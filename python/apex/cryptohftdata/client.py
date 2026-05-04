"""Minimal REST client for CryptoHFTData downloads."""

from __future__ import annotations

import json
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path


class CryptoHFTDataError(RuntimeError):
    pass


@dataclass
class DownloadResult:
    remote_path: str
    local_path: Path
    status: str
    bytes_written: int = 0


class CryptoHFTDataClient:
    def __init__(self, api_key: str | None, base_url: str = "https://api.cryptohftdata.com", timeout: int = 60):
        self.api_key = api_key
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout
        self.jwt_token: str | None = None

    def _base_headers(self) -> dict[str, str]:
        return {"User-Agent": "apex-cryptohftdata-preparer/1.0", "Accept": "application/json"}

    def authenticate(self) -> None:
        if not self.api_key:
            return
        headers = self._base_headers()
        headers.update({"Content-Type": "application/json", "X-API-Key": self.api_key})
        request = urllib.request.Request(
            f"{self.base_url}/jwt-token",
            data=b"{}",
            method="POST",
            headers=headers,
        )
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                payload = json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as exc:
            raise CryptoHFTDataError(f"CryptoHFTData JWT request failed with HTTP {exc.code}") from exc
        except urllib.error.URLError as exc:
            raise CryptoHFTDataError(f"CryptoHFTData JWT request failed: {exc.reason}") from exc
        token = payload.get("jwt_token")
        if not token:
            raise CryptoHFTDataError("CryptoHFTData JWT response did not contain jwt_token")
        self.jwt_token = str(token)

    def _headers(self) -> dict[str, str]:
        headers = self._base_headers()
        if self.jwt_token:
            headers["Authorization"] = f"Bearer {self.jwt_token}"
        return headers

    def list_symbols(self, exchange: str, data_type: str) -> list[str]:
        query = urllib.parse.urlencode({"exchange": exchange, "data_type": data_type})
        request = urllib.request.Request(f"{self.base_url}/symbols?{query}", headers=self._headers())
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                payload = json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as exc:
            raise CryptoHFTDataError(f"symbol discovery failed with HTTP {exc.code}") from exc
        except urllib.error.URLError as exc:
            raise CryptoHFTDataError(f"symbol discovery failed: {exc.reason}") from exc
        return [str(symbol).upper() for symbol in payload.get("symbols", [])]

    def download_file(self, remote_path: str, local_path: Path, retries: int = 3) -> DownloadResult:
        local_path.parent.mkdir(parents=True, exist_ok=True)
        query = urllib.parse.urlencode({"file": remote_path})
        url = f"{self.base_url}/download?{query}"
        tmp_path = local_path.with_name(local_path.name + ".tmp")
        last_error: Exception | None = None
        for attempt in range(retries + 1):
            try:
                request = urllib.request.Request(url, headers=self._headers())
                with urllib.request.urlopen(request, timeout=self.timeout) as response:
                    data = response.read()
                tmp_path.write_bytes(data)
                tmp_path.replace(local_path)
                return DownloadResult(remote_path, local_path, "downloaded", len(data))
            except urllib.error.HTTPError as exc:
                if exc.code == 404:
                    return DownloadResult(remote_path, local_path, "missing", 0)
                last_error = exc
            except urllib.error.URLError as exc:
                last_error = exc
            if attempt < retries:
                time.sleep(min(2 ** attempt, 8))
        raise CryptoHFTDataError(f"download failed for {remote_path}: {last_error}") from last_error
