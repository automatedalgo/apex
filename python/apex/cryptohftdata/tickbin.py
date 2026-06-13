"""Writer and verifier for Apex tickbin1 files."""

from __future__ import annotations

import json
import math
import os
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator, Literal


TICK_VERSION = b"TICK1   "
PREAMBLE_LEAD_LEN = 16
PREAMBLE_BLOCK = 1024
HEADER_STRUCT = struct.Struct("<QBB")
L1_BODY_STRUCT = struct.Struct("<dddd")
TRADE_BODY_STRUCT = struct.Struct("<ddQc3s")
L1_RECORD_SIZE = HEADER_STRUCT.size + L1_BODY_STRUCT.size
TRADE_RECORD_SIZE = HEADER_STRUCT.size + TRADE_BODY_STRUCT.size
MSG_TYPE_COMPAT = 0


@dataclass(frozen=True)
class TradeTick:
    capture_time_us: int
    event_time_us: int
    price: float
    quantity: float
    side: Literal["buy", "sell", "none"]


@dataclass(frozen=True)
class L1Tick:
    capture_time_us: int
    bid_price: float
    bid_quantity: float
    ask_price: float
    ask_quantity: float


def _padded_size(value: int, target_len: int) -> bytes:
    text = f"{value:0{target_len}d}"
    if len(text) != target_len:
        raise ValueError(f"cannot encode {value} in {target_len} bytes")
    return text.encode("ascii")


def _preamble_size(meta_json: bytes) -> int:
    required = PREAMBLE_LEAD_LEN + len(meta_json) + 1
    return ((required // PREAMBLE_BLOCK) + 1) * PREAMBLE_BLOCK


def build_preamble(meta: dict[str, object]) -> bytes:
    meta_json = json.dumps(meta, separators=(",", ":"), sort_keys=True).encode("utf-8")
    size = _preamble_size(meta_json)
    preamble = bytearray(size)
    preamble[0:8] = TICK_VERSION
    preamble[8:16] = _padded_size(size, 8)
    preamble[16 : 16 + len(meta_json)] = meta_json
    return bytes(preamble)


def encode_side(side: str) -> bytes:
    if side == "buy":
        return b"b"
    if side == "sell":
        return b"s"
    return b" "


def decode_side(value: bytes) -> str:
    if value == b"b":
        return "buy"
    if value == b"s":
        return "sell"
    return "none"


def pack_trade(tick: TradeTick) -> bytes:
    header = HEADER_STRUCT.pack(tick.capture_time_us, MSG_TYPE_COMPAT, TRADE_RECORD_SIZE)
    body = TRADE_BODY_STRUCT.pack(
        float(tick.price),
        float(tick.quantity),
        int(tick.event_time_us),
        encode_side(tick.side),
        b"\0\0\0",
    )
    return header + body


def pack_l1(tick: L1Tick) -> bytes:
    header = HEADER_STRUCT.pack(tick.capture_time_us, MSG_TYPE_COMPAT, L1_RECORD_SIZE)
    body = L1_BODY_STRUCT.pack(
        float(tick.ask_price),
        float(tick.ask_quantity),
        float(tick.bid_price),
        float(tick.bid_quantity),
    )
    return header + body


class TickbinWriter:
    """Atomic writer for one Apex daily tickbin1 file."""

    def __init__(self, path: Path, meta: dict[str, object], overwrite: bool = False):
        self.path = path
        self.meta = meta
        self.overwrite = overwrite
        self.tmp_path = path.with_name(path.name + ".tmp")
        self._file = None
        self.count = 0
        self.first_time_us: int | None = None
        self.last_time_us: int | None = None

    def __enter__(self) -> "TickbinWriter":
        if self.path.exists() and not self.overwrite:
            raise FileExistsError(f"output file already exists: {self.path}")
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._file = self.tmp_path.open("wb")
        self._file.write(build_preamble(self.meta))
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        assert self._file is not None
        self._file.close()
        if exc_type is None:
            os.replace(self.tmp_path, self.path)
        elif self.tmp_path.exists():
            self.tmp_path.unlink()

    def _track(self, capture_time_us: int) -> None:
        if self.first_time_us is None:
            self.first_time_us = capture_time_us
        self.last_time_us = capture_time_us
        self.count += 1

    def write_trade(self, tick: TradeTick) -> None:
        assert self._file is not None
        self._file.write(pack_trade(tick))
        self._track(tick.capture_time_us)

    def write_l1(self, tick: L1Tick) -> None:
        assert self._file is not None
        self._file.write(pack_l1(tick))
        self._track(tick.capture_time_us)


def read_preamble(path: Path) -> tuple[int, dict[str, object]]:
    with path.open("rb") as f:
        lead = f.read(PREAMBLE_LEAD_LEN)
        if len(lead) != PREAMBLE_LEAD_LEN:
            raise ValueError(f"incomplete tickbin header in {path}")
        version = lead[:8].rstrip()
        if version != b"TICK1":
            raise ValueError(f"unexpected tickbin version {version!r} in {path}")
        size = int(lead[8:16].decode("ascii"))
        rest = f.read(size - PREAMBLE_LEAD_LEN)
    raw_meta = rest.split(b"\0", 1)[0]
    return size, json.loads(raw_meta.decode("utf-8"))


def iter_records(path: Path, stream: Literal["aggtrades", "l1"]) -> Iterator[object]:
    header_len, _ = read_preamble(path)
    with path.open("rb") as f:
        f.seek(header_len)
        while True:
            header = f.read(HEADER_STRUCT.size)
            if not header:
                break
            if len(header) != HEADER_STRUCT.size:
                raise ValueError(f"incomplete record header in {path}")
            capture_time_us, _msg_type, size = HEADER_STRUCT.unpack(header)
            body = f.read(size - HEADER_STRUCT.size)
            if len(body) != size - HEADER_STRUCT.size:
                raise ValueError(f"incomplete record body in {path}")
            if stream == "aggtrades":
                if size != TRADE_RECORD_SIZE:
                    raise ValueError(f"unexpected trade record size {size} in {path}")
                price, qty, event_time_us, side, _pad = TRADE_BODY_STRUCT.unpack(body)
                yield TradeTick(capture_time_us, event_time_us, price, qty, decode_side(side))
            else:
                if size != L1_RECORD_SIZE:
                    raise ValueError(f"unexpected l1 record size {size} in {path}")
                ask_price, ask_qty, bid_price, bid_qty = L1_BODY_STRUCT.unpack(body)
                yield L1Tick(capture_time_us, bid_price, bid_qty, ask_price, ask_qty)


def verify_tickbin_file(
    path: Path,
    stream: Literal["aggtrades", "l1"],
    from_us: int | None = None,
    upto_us: int | None = None,
) -> dict[str, object]:
    previous: int | None = None
    count = 0
    first: int | None = None
    last: int | None = None
    for record in iter_records(path, stream):
        capture_time_us = record.capture_time_us
        if previous is not None and capture_time_us < previous:
            raise ValueError(f"non-monotonic tickbin timestamps in {path}")
        if from_us is not None and capture_time_us < from_us:
            raise ValueError(f"record before requested range in {path}")
        if upto_us is not None and capture_time_us >= upto_us:
            raise ValueError(f"record at or after requested upto time in {path}")
        if stream == "l1":
            if not all(
                math.isfinite(value) and value > 0
                for value in (
                    record.bid_price,
                    record.bid_quantity,
                    record.ask_price,
                    record.ask_quantity,
                )
            ):
                raise ValueError(f"invalid l1 values in {path}")
        first = capture_time_us if first is None else first
        last = capture_time_us
        previous = capture_time_us
        count += 1
    return {"path": str(path), "stream": stream, "count": count, "first_time_us": first, "last_time_us": last}

