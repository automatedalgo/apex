from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import apex.cryptohftdata.prepare as prepare_module
from apex.cryptohftdata.client import DownloadResult
from apex.cryptohftdata.constants import normalize_stream, normalize_venue
from apex.cryptohftdata.convert import L1BookBuilder, convert_orderbook_rows, convert_trade_rows
from apex.cryptohftdata.prepare import PrepareConfig, build_hour_plan
from apex.cryptohftdata.tickbin import TickbinWriter, iter_records, read_preamble, verify_tickbin_file
from apex.cryptohftdata.time_range import epoch_us, iter_hours_half_open, parse_apex_datetime


class TimeRangeTests(unittest.TestCase):
    def test_iter_hours_uses_half_open_semantics(self) -> None:
        start = parse_apex_datetime("2025-08-01T01:30:00")
        upto = parse_apex_datetime("2025-08-01T03:00:00")

        hours = iter_hours_half_open(start, upto)

        self.assertEqual([hour.strftime("%Y-%m-%dT%H") for hour in hours], ["2025-08-01T01", "2025-08-01T02"])

    def test_normalizers_accept_supported_scope(self) -> None:
        self.assertEqual(normalize_venue("bybit"), "bybit")
        self.assertEqual(normalize_stream("trades"), "aggtrades")
        self.assertEqual(normalize_stream("l1"), "l1")


class PlanningTests(unittest.TestCase):
    def test_build_hour_plan_uses_cryptohftdata_paths(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            config = PrepareConfig(
                venues=["bybit"],
                symbols=["BTCUSDT"],
                streams=["aggtrades", "l1"],
                start=parse_apex_datetime("2025-08-01"),
                upto=parse_apex_datetime("2025-08-01T02:00:00"),
                output_dir=Path(tmp) / "tickdata",
                cache_dir=Path(tmp) / "raw",
            )

            plan = build_hour_plan(config, Path(tmp) / "raw")

        self.assertEqual(len(plan), 4)
        self.assertEqual(plan[0].remote_path, "bybit/2025-08-01/00/BTCUSDT_trades.parquet.zst")
        self.assertEqual(plan[1].remote_path, "bybit/2025-08-01/01/BTCUSDT_trades.parquet.zst")
        self.assertEqual(plan[2].remote_path, "bybit/2025-08-01/00/BTCUSDT_orderbook.parquet.zst")


class TickbinConversionTests(unittest.TestCase):
    def test_trade_conversion_writes_apex_tickbin_records(self) -> None:
        start = parse_apex_datetime("2025-08-01T00:00:00")
        from_us = epoch_us(start)
        upto_us = epoch_us(parse_apex_datetime("2025-08-01T00:01:00"))
        rows = [
            {
                "received_time": (from_us + 2) * 1000,
                "trade_time": from_us + 2,
                "trade_id": 2,
                "price": "101.5",
                "quantity": "0.25",
                "is_buyer_maker": False,
            },
            {
                "received_time": (from_us - 1) * 1000,
                "trade_time": from_us - 1,
                "trade_id": 1,
                "price": "99.0",
                "quantity": "1.0",
                "is_buyer_maker": True,
            },
        ]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "BTCUSDT.bin"
            with TickbinWriter(path, {"e": "binance", "c": "aggtrades", "s": "BTCUSDT"}) as writer:
                stats = convert_trade_rows(rows, writer, from_us, upto_us)

            records = list(iter_records(path, "aggtrades"))
            verification = verify_tickbin_file(path, "aggtrades", from_us, upto_us)

        self.assertEqual(stats.input_rows, 2)
        self.assertEqual(stats.emitted, 1)
        self.assertEqual(stats.skipped_outside_range, 1)
        self.assertEqual(verification["count"], 1)
        self.assertEqual(records[0].capture_time_us, from_us + 2)
        self.assertEqual(records[0].side, "buy")
        self.assertEqual(records[0].price, 101.5)
        self.assertEqual(records[0].quantity, 0.25)

    def test_orderbook_conversion_seeds_from_pre_range_snapshot(self) -> None:
        start = parse_apex_datetime("2025-08-01T00:00:00")
        from_us = epoch_us(start)
        upto_us = epoch_us(parse_apex_datetime("2025-08-01T00:01:00"))
        rows = [
            {
                "received_time": (from_us - 10) * 1000,
                "event_time": from_us - 10,
                "event_type": "snapshot",
                "side": "bid",
                "price": "100",
                "quantity": "1",
            },
            {
                "received_time": (from_us - 10) * 1000,
                "event_time": from_us - 10,
                "event_type": "snapshot",
                "side": "ask",
                "price": "101",
                "quantity": "2",
            },
            {
                "received_time": (from_us + 1) * 1000,
                "event_time": from_us + 1,
                "event_type": "update",
                "side": "bid",
                "price": "99",
                "quantity": "3",
            },
            {
                "received_time": (from_us + 2) * 1000,
                "event_time": from_us + 2,
                "event_type": "update",
                "side": "ask",
                "price": "100.5",
                "quantity": "4",
            },
        ]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "BTCUSDT.bin"
            with TickbinWriter(path, {"e": "binance", "c": "l1", "s": "BTCUSDT"}) as writer:
                stats = convert_orderbook_rows(rows, writer, L1BookBuilder(), from_us, upto_us)

            records = list(iter_records(path, "l1"))
            _header_len, meta = read_preamble(path)

        self.assertEqual(meta["c"], "l1")
        self.assertEqual(stats.input_rows, 4)
        self.assertEqual(stats.emitted, 2)
        self.assertEqual(records[0].capture_time_us, from_us + 1)
        self.assertEqual(records[0].bid_price, 100)
        self.assertEqual(records[0].ask_price, 101)
        self.assertEqual(records[1].ask_price, 100.5)


class PrepareDataTests(unittest.TestCase):
    def test_prepare_data_converts_and_deletes_raw_cache_after_success(self) -> None:
        start = parse_apex_datetime("2025-08-01T00:00:00")
        from_us = epoch_us(start)

        class FakeClient:
            def __init__(self, api_key, base_url="https://api.cryptohftdata.com"):
                self.api_key = api_key
                self.base_url = base_url

            def authenticate(self) -> None:
                return None

            def list_symbols(self, exchange: str, data_type: str) -> list[str]:
                return ["BTCUSDT"]

            def download_file(self, remote_path: str, local_path: Path) -> DownloadResult:
                local_path.parent.mkdir(parents=True, exist_ok=True)
                local_path.write_bytes(b"fake-parquet")
                return DownloadResult(remote_path, local_path, "downloaded", local_path.stat().st_size)

        def fake_read_rows(path: Path, columns) -> list[dict[str, object]]:
            if "trades" in path.name:
                return [
                    {
                        "received_time": (from_us + 1) * 1000,
                        "trade_time": from_us + 1,
                        "trade_id": 1,
                        "price": "100",
                        "quantity": "1",
                        "is_buyer_maker": False,
                    }
                ]
            return [
                {
                    "received_time": (from_us + 1) * 1000,
                    "event_time": from_us + 1,
                    "event_type": "snapshot",
                    "side": "bid",
                    "price": "99",
                    "quantity": "2",
                },
                {
                    "received_time": (from_us + 1) * 1000,
                    "event_time": from_us + 1,
                    "event_type": "snapshot",
                    "side": "ask",
                    "price": "101",
                    "quantity": "3",
                },
            ]

        old_client = prepare_module.CryptoHFTDataClient
        old_reader = prepare_module.read_parquet_rows
        prepare_module.CryptoHFTDataClient = FakeClient
        prepare_module.read_parquet_rows = fake_read_rows
        try:
            with tempfile.TemporaryDirectory() as tmp:
                output_dir = Path(tmp) / "tickdata"
                cache_dir = Path(tmp) / "raw"
                config = PrepareConfig(
                    venues=["binance"],
                    symbols=["BTCUSDT"],
                    streams=["aggtrades", "l1"],
                    start=start,
                    upto=parse_apex_datetime("2025-08-01T01:00:00"),
                    output_dir=output_dir,
                    cache_dir=cache_dir,
                    verify=True,
                )

                result = prepare_module.prepare_data(config)

                self.assertTrue(result["raw_deleted"])
                self.assertFalse(Path(result["raw_root"]).exists())
                self.assertTrue(Path(result["manifest"]).exists())
                self.assertTrue((output_dir / "tickbin1/binance/aggtrades/2025/08/01/BTCUSDT.bin").exists())
                self.assertTrue((output_dir / "tickbin1/binance/l1/2025/08/01/BTCUSDT.bin").exists())
        finally:
            prepare_module.CryptoHFTDataClient = old_client
            prepare_module.read_parquet_rows = old_reader


if __name__ == "__main__":
    unittest.main()
