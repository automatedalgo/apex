#!/usr/bin/env bash

set -e


SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
echo $SCRIPT_DIR
source "${SCRIPT_DIR}/../scripts/depends.env"
set -u

echo "==> CSV refdata files saving to [${APEX_HOME}]"


rm -v -f \
   tmp/binance_* \
   tmp/kucoin_*

mkdir -p ./tmp

cd "$SCRIPT_DIR"
python3 fetch_binance_refdata.py
python3 parse_binance_refdata.py
python3 fetch_kucoin_refdata.py
python3 parse_kucoin_refdata.py

python3 bybit_fetch_refdata.py
python3 bybit_parse_refdata.py

python3 combine_asset_files.py tmp/*assets.csv

# install into shared location

echo installing files into ${APEX_HOME}
path=${APEX_HOME}/data/refdata/instruments/$(date +%Y%m%d)/instruments-$(date +%Y%m%d).csv


mkdir -p $(dirname $path)
cp -v tmp/instruments.csv "$path"
cd ${APEX_HOME}/data/refdata/instruments && ln -vsnf "$path" instruments.csv
