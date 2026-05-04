"""End-to-end CryptoHFTData preparation for Apex backtests."""

from __future__ import annotations

import json
import os
import shutil
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Literal

from .client import CryptoHFTDataClient, DownloadResult
from .constants import STREAM_TO_DATA_TYPE, VENUES
from .convert import L1BookBuilder, convert_orderbook_rows, convert_trade_rows, merge_stats
from .parquet import read_parquet_rows
from .tickbin import TickbinWriter, verify_tickbin_file
from .time_range import bucket_string, epoch_us, iter_hours_half_open


@dataclass(frozen=True)
class HourPlan:
    venue: str
    exchange: str
    symbol: str
    stream: Literal["aggtrades", "l1"]
    data_type: str
    hour: datetime
    remote_path: str
    local_path: Path


@dataclass
class PrepareConfig:
    venues: list[str]
    symbols: list[str]
    streams: list[Literal["aggtrades", "l1"]]
    start: datetime
    upto: datetime
    output_dir: Path
    api_key: str | None = None
    cache_dir: Path | None = None
    jobs: int = 4
    keep_raw: bool = False
    overwrite: bool = False
    verify: bool = True
    allow_missing_hours: bool = False
    refdata_csv: Path | None = None
    base_url: str = "https://api.cryptohftdata.com"


def apex_home() -> Path:
    return Path(os.environ.get("APEX_HOME", str(Path.home() / "apex"))).expanduser()


def default_tickdata_dir() -> Path:
    return apex_home() / "data" / "tickdata"


def default_refdata_csv() -> Path:
    return apex_home() / "data" / "refdata" / "instruments" / "instruments.csv"


def _remote_path(exchange: str, symbol: str, data_type: str, hour: datetime) -> str:
    return f"{exchange}/{hour:%Y-%m-%d}/{hour:%H}/{symbol}_{data_type}.parquet.zst"


def _local_path(raw_root: Path, remote_path: str) -> Path:
    return raw_root / remote_path


def build_hour_plan(config: PrepareConfig, raw_root: Path) -> list[HourPlan]:
    hours = iter_hours_half_open(config.start, config.upto)
    plan: list[HourPlan] = []
    for venue in config.venues:
        exchange = VENUES[venue].cryptohftdata_exchange
        for symbol in config.symbols:
            upper_symbol = symbol.upper()
            for stream in config.streams:
                data_type = STREAM_TO_DATA_TYPE[stream]
                for hour in hours:
                    remote = _remote_path(exchange, upper_symbol, data_type, hour)
                    plan.append(
                        HourPlan(
                            venue=venue,
                            exchange=exchange,
                            symbol=upper_symbol,
                            stream=stream,
                            data_type=data_type,
                            hour=hour,
                            remote_path=remote,
                            local_path=_local_path(raw_root, remote),
                        )
                    )
    return plan


def _load_refdata(path: Path | None) -> dict[tuple[str, str], str]:
    if path is None or not path.exists():
        return {}
    import csv

    mapping: dict[tuple[str, str], str] = {}
    with path.open(newline="") as f:
        for row in csv.DictReader(f):
            venue = row.get("venue", "")
            symbol = row.get("symbol", "")
            inst_id = row.get("instId", "")
            if venue and symbol and inst_id:
                mapping[(venue, symbol.upper())] = inst_id
    return mapping


def _validate_symbols(client: CryptoHFTDataClient, config: PrepareConfig) -> dict[str, object]:
    errors: list[str] = []
    checked: dict[str, list[str]] = {}
    for venue in config.venues:
        exchange = VENUES[venue].cryptohftdata_exchange
        for stream in config.streams:
            data_type = STREAM_TO_DATA_TYPE[stream]
            key = f"{exchange}:{data_type}"
            available = set(client.list_symbols(exchange, data_type))
            checked[key] = sorted(available)
            for symbol in config.symbols:
                if symbol.upper() not in available:
                    errors.append(f"{symbol.upper()} is not available for {exchange}/{data_type}")
    if errors:
        raise ValueError("; ".join(errors))
    return {"checked": sorted(checked)}


def _download_all(client: CryptoHFTDataClient, plan: list[HourPlan], jobs: int) -> list[DownloadResult]:
    results: list[DownloadResult] = []
    with ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        futures = [pool.submit(client.download_file, item.remote_path, item.local_path) for item in plan]
        for future in as_completed(futures):
            results.append(future.result())
    return results


def _output_path(output_dir: Path, venue: str, stream: str, day_key: tuple[int, int, int], symbol: str) -> Path:
    year, month, day = day_key
    return output_dir / "tickbin1" / venue / stream / f"{year:04d}" / f"{month:02d}" / f"{day:02d}" / f"{symbol}.bin"


def _plans_by_output_day(plan: list[HourPlan]) -> dict[tuple[str, str, str, tuple[int, int, int]], list[HourPlan]]:
    grouped: dict[tuple[str, str, str, tuple[int, int, int]], list[HourPlan]] = {}
    for item in plan:
        key = (item.venue, item.symbol, item.stream, (item.hour.year, item.hour.month, item.hour.day))
        grouped.setdefault(key, []).append(item)
    return grouped


def _required_columns(stream: str) -> list[str]:
    if stream == "aggtrades":
        return ["received_time", "event_time", "trade_time", "trade_id", "price", "quantity", "is_buyer_maker"]
    return [
        "received_time",
        "event_time",
        "event_type",
        "first_update_id",
        "final_update_id",
        "last_update_id",
        "side",
        "price",
        "quantity",
    ]


def _convert_outputs(
    config: PrepareConfig,
    plan: list[HourPlan],
    downloads: list[DownloadResult],
    refdata: dict[tuple[str, str], str],
) -> list[dict[str, object]]:
    from_us = epoch_us(config.start)
    upto_us = epoch_us(config.upto)
    missing = {result.remote_path for result in downloads if result.status == "missing"}
    converted: list[dict[str, object]] = []

    for (venue, symbol, stream, day_key), items in sorted(_plans_by_output_day(plan).items()):
        missing_for_output = [item.remote_path for item in items if item.remote_path in missing]
        if missing_for_output and not config.allow_missing_hours:
            raise FileNotFoundError(f"missing CryptoHFTData hours for {venue}/{symbol}/{stream}: {missing_for_output[:3]}")

        path = _output_path(config.output_dir, venue, stream, day_key, symbol)
        inst_id = refdata.get((venue, symbol), symbol)
        meta = {
            "e": venue,
            "c": stream,
            "s": symbol,
            "i": inst_id,
            "bin": bucket_string(day_key),
            "cm": {
                "source": "cryptohftdata",
                "range": "[from,upto)",
                "from": config.start.isoformat(),
                "upto": config.upto.isoformat(),
            },
        }
        stats_items = []
        with TickbinWriter(path, meta, overwrite=config.overwrite) as writer:
            book = L1BookBuilder()
            for item in sorted(items, key=lambda p: p.hour):
                if item.remote_path in missing:
                    continue
                rows = read_parquet_rows(item.local_path, _required_columns(stream))
                if stream == "aggtrades":
                    stats_items.append(convert_trade_rows(rows, writer, from_us, upto_us))
                else:
                    stats_items.append(convert_orderbook_rows(rows, writer, book, from_us, upto_us))
            merged = merge_stats(stats_items)
        verification = verify_tickbin_file(path, stream, from_us, upto_us) if config.verify else {}
        converted.append(
            {
                "venue": venue,
                "symbol": symbol,
                "stream": stream,
                "day": bucket_string(day_key),
                "path": str(path),
                "input_rows": merged.input_rows,
                "emitted": merged.emitted,
                "skipped_outside_range": merged.skipped_outside_range,
                "warning_count": merged.warning_count,
                "warnings": merged.warnings,
                "missing_hours": missing_for_output,
                "verification": verification,
            }
        )
    return converted


def _write_manifest(config: PrepareConfig, raw_root: Path, downloads: list[DownloadResult], outputs: list[dict[str, object]]) -> Path:
    manifest_dir = config.output_dir / "cryptohftdata-manifests"
    manifest_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = manifest_dir / f"prepare-{config.start:%Y%m%dT%H%M%S}-{config.upto:%Y%m%dT%H%M%S}.json"
    payload = {
        "source": "cryptohftdata",
        "range_semantics": "[from,upto)",
        "from": config.start.isoformat(),
        "upto": config.upto.isoformat(),
        "venues": config.venues,
        "symbols": [symbol.upper() for symbol in config.symbols],
        "streams": config.streams,
        "raw_root": str(raw_root),
        "raw_deleted_after_success": not config.keep_raw,
        "downloads": [
            {
                "remote_path": result.remote_path,
                "local_path": str(result.local_path),
                "status": result.status,
                "bytes_written": result.bytes_written,
            }
            for result in sorted(downloads, key=lambda item: item.remote_path)
        ],
        "outputs": outputs,
    }
    manifest_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    return manifest_path


def prepare_data(config: PrepareConfig) -> dict[str, object]:
    raw_base = config.cache_dir or (config.output_dir / "cryptohftdata-raw")
    raw_base.mkdir(parents=True, exist_ok=True)
    raw_root = Path(tempfile.mkdtemp(prefix="prepare-", dir=str(raw_base)))

    success = False
    try:
        client = CryptoHFTDataClient(config.api_key, base_url=config.base_url)
        client.authenticate()
        symbol_check = _validate_symbols(client, config)
        plan = build_hour_plan(config, raw_root)
        downloads = _download_all(client, plan, config.jobs)
        refdata = _load_refdata(config.refdata_csv)
        outputs = _convert_outputs(config, plan, downloads, refdata)
        manifest_path = _write_manifest(config, raw_root, downloads, outputs)
        success = True
        return {
            "manifest": str(manifest_path),
            "outputs": outputs,
            "downloads": len(downloads),
            "symbol_check": symbol_check,
            "raw_root": str(raw_root),
            "raw_deleted": not config.keep_raw,
        }
    finally:
        if success and not config.keep_raw:
            shutil.rmtree(raw_root, ignore_errors=True)
