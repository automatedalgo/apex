# CryptoHFTData Backtest Walkthrough

This walkthrough prepares one day of CryptoHFTData historical market data and
runs the standalone Apex market-making backtest against it. It uses Apex-style
half-open time semantics: `[from, upto)`.

The example range is:

```text
[2026-05-03T00:00:00, 2026-05-04T00:00:00)
```

That range downloads the UTC day `2026-05-03` and excludes all events at or
after `2026-05-04T00:00:00`.

## 1. Install Python Dependencies

From the repository root:

```shell
python3 -m pip install -r python/requirements-cryptohftdata.txt
```

The converter uses `pyarrow` to read Parquet and `zstandard` when a downloaded
file is wrapped in whole-file Zstandard compression.

## 2. Configure the Run

Set the CryptoHFTData key and the dataset variables:

```shell
export CRYPTOHFTDATA_API_KEY="your-api-key"
export APEX_HOME="${APEX_HOME:-$HOME/apex}"

export APEX_CHD_VENUE="binance_usdfut"
export APEX_CHD_SYMBOL="BTCUSDT"
export APEX_CHD_FROM="2026-05-03T00:00:00"
export APEX_CHD_UPTO="2026-05-04T00:00:00"
```

The API key is used only to request a short-lived JWT. It is not written to the
manifest, tick data, or logs.

## 3. Generate Reference Data

Apex needs instrument reference data before it can run a strategy:

```shell
cd python
./generate-refdata.sh
cd ..
```

This writes:

```text
$APEX_HOME/data/refdata/instruments/instruments.csv
```

## 4. Build the Backtest Example

Build the standalone example once:

```shell
cmake --build BUILD/debug --target apex-example-cryptohftdata-backtest
```

If your local build directory is different, set it explicitly:

```shell
export APEX_CHD_BUILD_DIR="$PWD/BUILD/debug"
```

For example, a local debug build may use:

```shell
export APEX_CHD_BUILD_DIR="$PWD/BUILD/cryptohft-debug"
cmake --build "$APEX_CHD_BUILD_DIR" --target apex-example-cryptohftdata-backtest
```

## 5. Prepare CryptoHFTData for Apex

Run the preparation CLI:

```shell
python3 python/prepare_cryptohftdata.py \
  --venues "$APEX_CHD_VENUE" \
  --symbols "$APEX_CHD_SYMBOL" \
  --streams aggtrades,l1 \
  --from "$APEX_CHD_FROM" \
  --upto "$APEX_CHD_UPTO" \
  --jobs 4 \
  --overwrite
```

The tool downloads the hourly CryptoHFTData files, converts them into daily Apex
`tickbin1` files, verifies the converted files, writes a manifest, and deletes
the temporary raw downloads after successful conversion.

The download phase is usually quick. The conversion phase can still take several
minutes for a full day of `l1` data because it scans tens of millions of
orderbook rows in Python, rebuilds top-of-book state, writes `tickbin1`, and
verifies the output by reading it back.

Prepared files are written under:

```text
$APEX_HOME/data/tickdata/tickbin1/binance_usdfut/aggtrades/2026/05/03/BTCUSDT.bin
$APEX_HOME/data/tickdata/tickbin1/binance_usdfut/l1/2026/05/03/BTCUSDT.bin
```

## 6. Inspect the Manifest

The manifest is written under:

```text
$APEX_HOME/data/tickdata/cryptohftdata-manifests/
```

Print a concise summary:

```shell
python3 - <<'PY'
import json
import os
from pathlib import Path

apex_home = Path(os.environ.get("APEX_HOME", Path.home() / "apex")).expanduser()
manifest_dir = apex_home / "data" / "tickdata" / "cryptohftdata-manifests"
manifest_path = sorted(manifest_dir.glob("prepare-*.json"))[-1]
manifest = json.loads(manifest_path.read_text())

print("manifest:", manifest_path)
print("range:", manifest["from"], manifest["upto"], manifest["range_semantics"])
print("raw deleted after success:", manifest["raw_deleted_after_success"])
print("downloads:", len(manifest["downloads"]))
for item in manifest["outputs"]:
    print(
        item["venue"],
        item["symbol"],
        item["stream"],
        "emitted=" + str(item["emitted"]),
        "warnings=" + str(item.get("warning_count", len(item["warnings"]))),
        "verified=" + str(item["verification"].get("count")),
    )
PY
```

A verified `binance_usdfut BTCUSDT` run for `2026-05-03` produced:

```text
downloads: 48
binance_usdfut BTCUSDT aggtrades emitted=1728528 warnings=0 verified=1728528
binance_usdfut BTCUSDT l1 emitted=902721 warnings=1058957 verified=902721
```

The large `l1` warning count is mostly repeated `orderbook update before first
snapshot` warnings. It means the converter saw update rows before an explicit
snapshot marker. The converted file still verifies, but the warning is useful
when judging the data source semantics for a range.

## 7. Run the Backtest

Run the example over the same half-open range:

```shell
"${APEX_CHD_BUILD_DIR:-$PWD/BUILD/debug}/src/examples/standalone/apex-example-cryptohftdata-backtest"
```

The example reads these environment variables:

```text
APEX_CHD_VENUE
APEX_CHD_SYMBOL
APEX_CHD_FROM
APEX_CHD_UPTO
```

A successful run ends with:

```text
backtest reached end time -- backtest complete
```

## 8. Bybit Variation

To prepare Bybit linear BTCUSDT instead, keep the same workflow and change the
venue:

```shell
export APEX_CHD_VENUE="bybit"
export APEX_CHD_SYMBOL="BTCUSDT"

python3 python/prepare_cryptohftdata.py \
  --venues "$APEX_CHD_VENUE" \
  --symbols "$APEX_CHD_SYMBOL" \
  --streams aggtrades,l1 \
  --from "$APEX_CHD_FROM" \
  --upto "$APEX_CHD_UPTO" \
  --jobs 4 \
  --overwrite
```

Confirm that the selected Bybit symbol exists in CryptoHFTData for both
`trades` and `orderbook`. The CLI validates symbols before downloading data.

## Troubleshooting

* `set CRYPTOHFTDATA_API_KEY`: Export the API key or pass `--api-key-file`.
* `output file already exists`: Add `--overwrite` only when replacing the old
  prepared data is intentional.
* `missing CryptoHFTData hours`: Check the date, symbol, and exchange data
  availability. Use `--allow-missing-hours` only for partial-data experiments.
* No ticks in the backtest: Confirm the prepared path under `tickbin1` matches
  the venue, stream, date, and symbol used by the example.
* CMake cannot find the example binary: Set `APEX_CHD_BUILD_DIR` to the build
  directory that contains `src/examples/standalone/apex-example-cryptohftdata-backtest`.
