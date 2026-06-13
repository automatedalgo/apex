"""Conversion from CryptoHFTData rows to Apex tickbin ticks."""

from __future__ import annotations

import heapq
from collections.abc import Iterable, Mapping
from dataclasses import dataclass, field
from typing import Any, Literal

from .tickbin import L1Tick, TickbinWriter, TradeTick
from .time_range import received_time_to_epoch_us, timestamp_to_epoch_us


Row = Mapping[str, Any]
WARNING_SAMPLE_LIMIT = 100


@dataclass
class ConversionStats:
    input_rows: int = 0
    emitted: int = 0
    skipped_outside_range: int = 0
    warnings: list[str] = field(default_factory=list)
    warning_count: int = 0

    def add_warning(self, message: str) -> None:
        self.warning_count += 1
        if len(self.warnings) < WARNING_SAMPLE_LIMIT:
            self.warnings.append(message)


def _float_field(row: Row, field: str) -> float:
    value = row.get(field)
    if value is None:
        raise ValueError(f"missing required field '{field}'")
    return float(value)


def _received_us(row: Row) -> int:
    return received_time_to_epoch_us(row.get("received_time"))


def _trade_event_time_us(row: Row, capture_time_us: int) -> int:
    for field in ("trade_time", "event_time", "transaction_time"):
        value = row.get(field)
        if value is not None:
            return timestamp_to_epoch_us(value)
    return capture_time_us


def _trade_side(row: Row) -> Literal["buy", "sell", "none"]:
    marker = row.get("is_buyer_maker")
    if marker is True:
        return "sell"
    if marker is False:
        return "buy"
    side = str(row.get("side", "")).strip().lower()
    if side in ("buy", "sell"):
        return side  # type: ignore[return-value]
    return "none"


def _sorted_rows(rows: Iterable[Row], key_fields: tuple[str, ...]) -> list[Row]:
    loaded = list(rows)
    def key(row: Row) -> tuple[Any, ...]:
        values: list[Any] = [_received_us(row)]
        for field in key_fields:
            values.append(row.get(field))
        return tuple(values)

    if any(key(loaded[i]) < key(loaded[i - 1]) for i in range(1, len(loaded))):
        loaded.sort(key=key)
    return loaded


def convert_trade_rows(rows: Iterable[Row], writer: TickbinWriter, from_us: int, upto_us: int) -> ConversionStats:
    stats = ConversionStats()
    for row in _sorted_rows(rows, ("trade_time", "trade_id")):
        stats.input_rows += 1
        capture_time_us = _received_us(row)
        if capture_time_us < from_us or capture_time_us >= upto_us:
            stats.skipped_outside_range += 1
            continue
        writer.write_trade(
            TradeTick(
                capture_time_us=capture_time_us,
                event_time_us=_trade_event_time_us(row, capture_time_us),
                price=_float_field(row, "price"),
                quantity=_float_field(row, "quantity"),
                side=_trade_side(row),
            )
        )
        stats.emitted += 1
    return stats


class _BookSide:
    """Heap-backed book side with lazy removal for efficient best-price lookup."""

    def __init__(self, side: Literal["bid", "ask"]):
        self.side = side
        self.levels: dict[float, float] = {}
        self.heap: list[float] = []

    def clear(self) -> None:
        self.levels.clear()
        self.heap.clear()

    def update(self, price: float, quantity: float) -> None:
        if quantity <= 0:
            self.levels.pop(price, None)
            return
        self.levels[price] = quantity
        heapq.heappush(self.heap, -price if self.side == "bid" else price)

    def best(self) -> tuple[float, float] | None:
        while self.heap:
            price = -self.heap[0] if self.side == "bid" else self.heap[0]
            qty = self.levels.get(price)
            if qty is not None and qty > 0:
                return price, qty
            heapq.heappop(self.heap)
        return None


class L1BookBuilder:
    def __init__(self) -> None:
        self.bids = _BookSide("bid")
        self.asks = _BookSide("ask")
        self.last_top: tuple[float, float, float, float] | None = None
        self.have_snapshot = False

    def clear(self) -> None:
        self.bids.clear()
        self.asks.clear()
        self.last_top = None
        self.have_snapshot = True

    def apply(self, row: Row) -> None:
        side = str(row.get("side", "")).strip().lower()
        price = _float_field(row, "price")
        quantity = _float_field(row, "quantity")
        if side == "bid":
            self.bids.update(price, quantity)
        elif side == "ask":
            self.asks.update(price, quantity)
        else:
            raise ValueError(f"unsupported orderbook side '{side}'")

    def maybe_tick(self, capture_time_us: int) -> L1Tick | None:
        bid = self.bids.best()
        ask = self.asks.best()
        if bid is None or ask is None:
            return None
        top = (bid[0], bid[1], ask[0], ask[1])
        if top == self.last_top:
            return None
        self.last_top = top
        return L1Tick(
            capture_time_us=capture_time_us,
            bid_price=bid[0],
            bid_quantity=bid[1],
            ask_price=ask[0],
            ask_quantity=ask[1],
        )


def _orderbook_group_key(row: Row) -> tuple[int, str, Any, Any, Any]:
    return (
        _received_us(row),
        str(row.get("event_type", "")).strip().lower(),
        row.get("first_update_id"),
        row.get("final_update_id"),
        row.get("last_update_id"),
    )


def convert_orderbook_rows(
    rows: Iterable[Row],
    writer: TickbinWriter,
    book: L1BookBuilder,
    from_us: int,
    upto_us: int,
) -> ConversionStats:
    stats = ConversionStats()
    sorted_rows = _sorted_rows(rows, ("event_time", "first_update_id", "final_update_id", "side", "price"))
    group: list[Row] = []
    current_key: tuple[int, str, Any, Any, Any] | None = None

    def flush_group() -> None:
        nonlocal group, current_key
        if not group or current_key is None:
            return
        capture_time_us, event_type, *_ = current_key
        stats.input_rows += len(group)
        if capture_time_us >= upto_us:
            stats.skipped_outside_range += len(group)
            group = []
            current_key = None
            return
        if event_type == "snapshot":
            book.clear()
        elif not book.have_snapshot:
            stats.add_warning(f"orderbook update before first snapshot at {capture_time_us}")
        for row in group:
            book.apply(row)
        if capture_time_us < from_us:
            # Pre-range book events seed the book so the first emitted L1 after
            # from_us reflects current state, but they must not create records.
            stats.skipped_outside_range += len(group)
            group = []
            current_key = None
            return
        tick = book.maybe_tick(capture_time_us)
        if tick is not None:
            if tick.bid_price >= tick.ask_price:
                stats.add_warning(f"crossed l1 skipped at {capture_time_us}")
            else:
                writer.write_l1(tick)
                stats.emitted += 1
        group = []
        current_key = None

    for row in sorted_rows:
        key = _orderbook_group_key(row)
        if current_key is None:
            current_key = key
        elif key != current_key:
            flush_group()
            current_key = key
        group.append(row)
    flush_group()
    return stats


def merge_stats(items: Iterable[ConversionStats]) -> ConversionStats:
    merged = ConversionStats()
    for item in items:
        merged.input_rows += item.input_rows
        merged.emitted += item.emitted
        merged.skipped_outside_range += item.skipped_outside_range
        merged.warning_count += item.warning_count
        remaining = WARNING_SAMPLE_LIMIT - len(merged.warnings)
        if remaining > 0:
            merged.warnings.extend(item.warnings[:remaining])
    return merged
