# CryptoHFTData Preparation

This guide explains how to prepare CryptoHFTData historical market data for Apex
backtests. The preparation step downloads CryptoHFTData hourly Parquet files,
converts them into Apex `tickbin1` daily files, verifies the result, and deletes
the raw downloaded files after a successful conversion.

## Supported Scope

The first implementation supports the Apex venues that already exist in the C++
engine:

| Apex venue | CryptoHFTData exchange | Notes |
| --- | --- | --- |
| `binance` | `binance_spot` | Binance spot symbols |
| `binance_usdfut` | `binance_futures` | Binance USD-margined futures |
| `bybit` | `bybit` | Bybit linear derivatives, matching Apex's current Bybit venue |

Supported streams:

| Apex stream | CryptoHFTData data type | Output path component |
| --- | --- | --- |
| `aggtrades` | `trades` | `aggtrades` |
| `l1` | `orderbook` | `l1` |

Full L2 replay, funding rates, open interest, liquidations, and unsupported
venues are intentionally out of scope for this preparation tool.

## Install Python Dependencies

From the repository root:

```shell
python3 -m pip install -r python/requirements-cryptohftdata.txt
```

`pyarrow` reads Parquet files. `zstandard` is used when a downloaded
`.parquet.zst` file is wrapped as a whole-file Zstandard payload.

## API Key Handling

Use an environment variable:

```shell
export CRYPTOHFTDATA_API_KEY="your-api-key"
```

Or store the key in a local file and pass `--api-key-file`:

```shell
python3 python/prepare_cryptohftdata.py \
  --api-key-file ~/.config/cryptohftdata/api-key \
  --venues binance_usdfut \
  --symbols BTCUSDT \
  --from 2025-08-01 \
  --upto 2025-08-02
```

The CLI requests a short-lived JWT with the API key and then uses the JWT in the
`Authorization` header. API keys are not written to logs, manifests, or download
URLs.

## Range Semantics

Time ranges use Apex-style half-open semantics: `[from, upto)`.

For example:

```shell
--from 2025-08-01 --upto 2025-08-03
```

includes events at or after `2025-08-01T00:00:00Z` and strictly before
`2025-08-03T00:00:00Z`. It does not include any event from `2025-08-03`.

The planner downloads every UTC hourly CryptoHFTData object that intersects the
half-open range, then filters records by event replay timestamp during
conversion.

## Quick Start

Prepare Binance futures BTCUSDT and ETHUSDT for two days:

```shell
export CRYPTOHFTDATA_API_KEY="your-api-key"

python3 python/prepare_cryptohftdata.py \
  --venues binance_usdfut \
  --symbols BTCUSDT,ETHUSDT \
  --streams aggtrades,l1 \
  --from 2025-08-01 \
  --upto 2025-08-03 \
  --jobs 8
```

Prepare Bybit data:

```shell
python3 python/prepare_cryptohftdata.py \
  --venues bybit \
  --symbols BTCUSDT \
  --streams aggtrades,l1 \
  --from 2025-08-01T12:00:00 \
  --upto 2025-08-01T18:00:00 \
  --jobs 8
```

By default, output is written under:

```text
$APEX_HOME/data/tickdata/tickbin1/{venue}/{stream}/YYYY/MM/DD/{symbol}.bin
```

If `APEX_HOME` is not set, Apex defaults to `~/apex`.

## Raw Cache Behavior

Raw CryptoHFTData files are downloaded into a temporary raw cache under:

```text
$APEX_HOME/data/tickdata/cryptohftdata-raw/
```

After all conversions and tickbin verification pass, the temporary raw cache for
that run is deleted automatically. If conversion fails, the raw files are left in
place for diagnosis.

Use `--keep-raw` to keep raw files even after success:

```shell
python3 python/prepare_cryptohftdata.py \
  --venues binance \
  --symbols BTCUSDT \
  --from 2025-08-01 \
  --upto 2025-08-02 \
  --keep-raw
```

Use `--cache-dir` to choose a different temporary raw cache root.

## Existing Output Files

The tool refuses to replace existing `.bin` files unless `--overwrite` is
provided:

```shell
python3 python/prepare_cryptohftdata.py \
  --venues binance_usdfut \
  --symbols BTCUSDT \
  --from 2025-08-01 \
  --upto 2025-08-02 \
  --overwrite
```

This avoids accidentally mixing old and newly prepared market data.

## Manifests

Each successful run writes a manifest under:

```text
$APEX_HOME/data/tickdata/cryptohftdata-manifests/
```

The manifest includes:

- requested venues, symbols, streams, and `[from, upto)` range
- each CryptoHFTData object path and download status
- each Apex output file path
- input row counts and emitted record counts
- missing hours and conversion warnings
- tickbin verification summaries

Secrets are never written to the manifest.

## Verification

Run Python tests:

```shell
PYTHONPATH=python python3 -m unittest discover python/tests
```

Run the CLI with verification enabled, which is the default:

```shell
python3 python/prepare_cryptohftdata.py \
  --venues binance_usdfut \
  --symbols BTCUSDT \
  --from 2025-08-01 \
  --upto 2025-08-02
```

Then run Apex tests and a backtest using the generated files:

```shell
cmake --build BUILD/debug
ctest --test-dir BUILD/debug
./BUILD/debug/src/examples/standalone/standalone_algo_backtest
```

Adjust the build directory if your local CMake preset uses a different path.

## Troubleshooting

- `missing CryptoHFTData hours`: At least one hourly object returned 404. Check
  the date range, symbol, and data availability. Use `--allow-missing-hours` only
  when partial data is acceptable.
- `output file already exists`: Re-run with `--overwrite` or remove the old
  prepared file intentionally.
- `pyarrow is required`: Install `python/requirements-cryptohftdata.txt`.
- `orderbook update before first snapshot`: The selected range starts before the
  file provides enough book state. The converter will emit L1 only after it can
  construct a valid bid/ask.
- No market data in backtest: Confirm that the Apex instrument native symbol and
  venue match the prepared path under `tickbin1/{venue}/{stream}/...`.

