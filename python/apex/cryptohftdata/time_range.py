"""UTC time parsing and half-open range expansion for Apex data prep."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import UTC, date, datetime, time, timedelta


MICROS_PER_SECOND = 1_000_000
NANOS_PER_MICRO = 1_000


@dataclass(frozen=True)
class HourRange:
    start: datetime
    upto: datetime

    def __post_init__(self) -> None:
        if self.start.tzinfo is None or self.upto.tzinfo is None:
            raise ValueError("HourRange requires timezone-aware UTC datetimes")
        if self.start >= self.upto:
            raise ValueError("from time must be before upto time")


def parse_apex_datetime(value: str) -> datetime:
    """Parse Apex-style date/time strings as UTC.

    Dates such as ``2025-08-01`` mean midnight UTC. Datetimes may include
    seconds, fractional seconds, and an optional trailing ``Z``.
    """

    text = value.strip()
    if not text:
        raise ValueError("empty timestamp")

    if len(text) == 10 and text[4] == "-" and text[7] == "-":
        return datetime.combine(date.fromisoformat(text), time(), tzinfo=UTC)

    if text.endswith("Z"):
        text = text[:-1] + "+00:00"
    elif "+" not in text[10:] and "-" not in text[10:]:
        text += "+00:00"

    parsed = datetime.fromisoformat(text)
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=UTC)
    return parsed.astimezone(UTC)


def epoch_us(dt: datetime) -> int:
    if dt.tzinfo is None:
        raise ValueError("epoch_us requires timezone-aware datetime")
    return int(dt.timestamp() * MICROS_PER_SECOND)


def datetime_from_epoch_us(value: int) -> datetime:
    return datetime.fromtimestamp(value / MICROS_PER_SECOND, tz=UTC)


def floor_hour(dt: datetime) -> datetime:
    return dt.astimezone(UTC).replace(minute=0, second=0, microsecond=0)


def iter_hours_half_open(start: datetime, upto: datetime) -> list[datetime]:
    """Return UTC hourly buckets intersecting the half-open range [start, upto)."""

    hour_range = HourRange(start.astimezone(UTC), upto.astimezone(UTC))
    current = floor_hour(hour_range.start)
    hours: list[datetime] = []
    while current < hour_range.upto:
        hours.append(current)
        current += timedelta(hours=1)
    return hours


def timestamp_to_epoch_us(value: object) -> int:
    """Infer seconds/ms/us/ns epoch units from magnitude and return microseconds."""

    if value is None:
        raise ValueError("timestamp value is missing")
    ivalue = int(value)
    magnitude = abs(ivalue)
    if magnitude >= 1_000_000_000_000_000_000:
        return ivalue // 1_000
    if magnitude >= 1_000_000_000_000_000:
        return ivalue
    if magnitude >= 1_000_000_000_000:
        return ivalue * 1_000
    if magnitude >= 1_000_000_000:
        return ivalue * MICROS_PER_SECOND
    raise ValueError(f"timestamp value {value!r} is too small to infer epoch unit")


def received_time_to_epoch_us(value: object) -> int:
    """CryptoHFTData documents received_time as nanoseconds."""

    if value is None:
        raise ValueError("received_time is missing")
    return int(value) // NANOS_PER_MICRO


def day_key_from_epoch_us(value: int) -> tuple[int, int, int]:
    dt = datetime_from_epoch_us(value)
    return dt.year, dt.month, dt.day


def bucket_string(day_key: tuple[int, int, int]) -> str:
    year, month, day = day_key
    return f"{year:04d}{month:02d}{day:02d}"

