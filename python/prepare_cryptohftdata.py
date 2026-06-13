#!/usr/bin/env python3
"""Prepare CryptoHFTData files for Apex backtests."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

from apex.cryptohftdata.constants import normalize_stream, normalize_venue, split_csv
from apex.cryptohftdata.prepare import PrepareConfig, default_refdata_csv, default_tickdata_dir, prepare_data
from apex.cryptohftdata.time_range import parse_apex_datetime


def _dedupe(values: list[str]) -> list[str]:
    seen: set[str] = set()
    result: list[str] = []
    for value in values:
        if value not in seen:
            result.append(value)
            seen.add(value)
    return result


def _api_key_from_args(args: argparse.Namespace) -> str | None:
    if args.anonymous:
        return None
    if args.api_key_file:
        return Path(args.api_key_file).expanduser().read_text().strip()
    value = os.environ.get("CRYPTOHFTDATA_API_KEY")
    if value:
        return value.strip()
    raise SystemExit("set CRYPTOHFTDATA_API_KEY, pass --api-key-file, or use --anonymous")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--venues", required=True, help="Comma-separated Apex venues: binance,binance_usdfut,bybit")
    parser.add_argument("--symbols", required=True, help="Comma-separated native symbols, e.g. BTCUSDT,ETHUSDT")
    parser.add_argument("--streams", default="aggtrades,l1", help="Comma-separated streams: aggtrades,l1")
    parser.add_argument("--from", dest="from_time", required=True, help="Inclusive UTC start, e.g. 2025-08-01")
    parser.add_argument("--upto", "--to", dest="upto_time", required=True, help="Exclusive UTC end, e.g. 2025-08-02")
    parser.add_argument("--output-dir", type=Path, default=default_tickdata_dir(), help="Apex tickdata root")
    parser.add_argument("--cache-dir", type=Path, default=None, help="Temporary raw download root")
    parser.add_argument("--api-key-file", type=Path, default=None, help="File containing CryptoHFTData API key")
    parser.add_argument("--anonymous", action="store_true", help="Use the unauthenticated free tier")
    parser.add_argument("--jobs", type=int, default=4, help="Concurrent downloads")
    parser.add_argument("--keep-raw", action="store_true", help="Retain raw CryptoHFTData files after success")
    parser.add_argument("--overwrite", action="store_true", help="Replace existing tickbin output files")
    parser.add_argument("--no-verify", action="store_true", help="Skip Python tickbin verification")
    parser.add_argument("--allow-missing-hours", action="store_true", help="Convert available hours instead of failing on 404")
    parser.add_argument("--refdata-csv", type=Path, default=default_refdata_csv(), help="Apex instruments.csv for metadata")
    parser.add_argument("--base-url", default="https://api.cryptohftdata.com", help=argparse.SUPPRESS)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    venues = _dedupe([normalize_venue(value) for value in split_csv(args.venues)])
    symbols = _dedupe([symbol.upper() for symbol in split_csv(args.symbols)])
    streams = _dedupe([normalize_stream(value) for value in split_csv(args.streams)])
    if not venues or not symbols or not streams:
        parser.error("--venues, --symbols, and --streams must each contain at least one value")
    start = parse_apex_datetime(args.from_time)
    upto = parse_apex_datetime(args.upto_time)
    if start >= upto:
        parser.error("--from must be before --upto")
    config = PrepareConfig(
        venues=venues,
        symbols=symbols,
        streams=streams,
        start=start,
        upto=upto,
        output_dir=args.output_dir.expanduser(),
        api_key=_api_key_from_args(args),
        cache_dir=args.cache_dir.expanduser() if args.cache_dir else None,
        jobs=args.jobs,
        keep_raw=args.keep_raw,
        overwrite=args.overwrite,
        verify=not args.no_verify,
        allow_missing_hours=args.allow_missing_hours,
        refdata_csv=args.refdata_csv.expanduser() if args.refdata_csv else None,
        base_url=args.base_url,
    )
    result = prepare_data(config)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
