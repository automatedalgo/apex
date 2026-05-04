"""Static mapping between Apex concepts and CryptoHFTData datasets."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class VenueMapping:
    apex_venue: str
    cryptohftdata_exchange: str


VENUES: dict[str, VenueMapping] = {
    "binance": VenueMapping("binance", "binance_spot"),
    "binance_usdfut": VenueMapping("binance_usdfut", "binance_futures"),
    # Apex's current Bybit venue is the linear derivatives feed.
    "bybit": VenueMapping("bybit", "bybit"),
}

STREAM_TO_DATA_TYPE: dict[str, str] = {
    "aggtrades": "trades",
    "trades": "trades",
    "l1": "orderbook",
}

DATA_TYPE_TO_OUTPUT_STREAM: dict[str, str] = {
    "trades": "aggtrades",
    "orderbook": "l1",
}


def normalize_venue(value: str) -> str:
    venue = value.strip().lower()
    if venue not in VENUES:
        supported = ", ".join(sorted(VENUES))
        raise ValueError(f"unsupported venue '{value}', supported: {supported}")
    return venue


def normalize_stream(value: str) -> str:
    stream = value.strip().lower()
    if stream not in STREAM_TO_DATA_TYPE:
        supported = ", ".join(sorted(STREAM_TO_DATA_TYPE))
        raise ValueError(f"unsupported stream '{value}', supported: {supported}")
    data_type = STREAM_TO_DATA_TYPE[stream]
    return DATA_TYPE_TO_OUTPUT_STREAM[data_type]


def split_csv(value: str) -> list[str]:
    return [item.strip() for item in value.split(",") if item.strip()]

