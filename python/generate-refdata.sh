#!/usr/bin/env bash

set -e


SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
echo $SCRIPT_DIR
source "${SCRIPT_DIR}/../scripts/depends.env"
set -u

echo "==> CSV refdata files saving to [${APEX_HOME}]"


rm -v -f \
   tmp/binance_exchange-info.json \
   tmp/binance_usdfut_exchange-info.json \
   tmp/binance_coinfut_exchange-info.json \
   tmp/binance_assets.csv

mkdir -p ./tmp

cd "$SCRIPT_DIR"
python3 fetch_binance_refdata.py
python3 parse_binance_refdata.py

# install into shared location

echo installing files into ${APEX_HOME}
path=${APEX_HOME}/data/refdata/assets/$(date +%Y%m%d)/assets-$(date +%Y%m%d).csv


mkdir -p $(dirname $path)
cp -v tmp/binance_assets.csv "$path"
cd ${APEX_HOME}/data/refdata/assets && ln -vsnf "$path" assets-latest.csv
