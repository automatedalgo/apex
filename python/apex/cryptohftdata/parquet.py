"""Parquet loading helpers for CryptoHFTData hourly files."""

from __future__ import annotations

from io import BytesIO
from pathlib import Path
from typing import Iterable


def read_parquet_rows(path: Path, columns: Iterable[str] | None = None) -> list[dict[str, object]]:
    """Read a CryptoHFTData ``.parquet.zst`` file into row dictionaries.

    The REST documentation describes files as Parquet with Zstandard
    compression. Some providers use Parquet's native column compression, while
    others wrap the whole Parquet file in zstd. This helper supports both.
    """

    try:
        import pyarrow.parquet as pq
    except ImportError as exc:
        raise RuntimeError("pyarrow is required: python3 -m pip install -r python/requirements-cryptohftdata.txt") from exc

    selected = list(columns) if columns is not None else None
    try:
        table = pq.read_table(path, columns=selected)
    except Exception as parquet_error:
        try:
            import zstandard as zstd
        except ImportError as exc:
            raise RuntimeError(
                "zstandard is required for whole-file .zst payloads: "
                "python3 -m pip install -r python/requirements-cryptohftdata.txt"
            ) from exc
        try:
            payload = zstd.ZstdDecompressor().decompress(path.read_bytes())
            table = pq.read_table(BytesIO(payload), columns=selected)
        except Exception as zstd_error:
            raise RuntimeError(f"failed to read CryptoHFTData parquet file {path}") from zstd_error
    return table.to_pylist()

