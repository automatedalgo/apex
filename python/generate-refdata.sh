#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
echo $SCRIPT_DIR
source "${SCRIPT_DIR}/../scripts/depends.env"

# for now on, expect all variables to be defined
set -u

tmp_dir=$(mktemp -d /tmp/apex-refdata.XXXXXX) # will later be deleted
echo "using temporary folder: $tmp_dir"

cd "$SCRIPT_DIR"

python3 fetch_binance_refdata.py --tmp "${tmp_dir}"
python3 parse_binance_refdata.py --tmp "${tmp_dir}"

python3 fetch_kucoin_refdata.py --tmp "${tmp_dir}"
python3 parse_kucoin_refdata.py --tmp "${tmp_dir}"

python3 bybit_fetch_refdata.py --tmp "${tmp_dir}"
python3 bybit_parse_refdata.py --tmp "${tmp_dir}"

python3 combine_asset_files.py --tmp "${tmp_dir}" "${tmp_dir}"/*assets.csv

# install into Apex shared location

echo "installing CSV refdata into [${APEX_HOME}]"

echo installing files into ${APEX_HOME}
path=${APEX_HOME}/data/refdata/instruments/$(date +%Y%m%d)/instruments-$(date +%Y%m%d).csv

mkdir -p $(dirname $path)
cp -v "${tmp_dir}"/instruments.csv "$path"
cd ${APEX_HOME}/data/refdata/instruments && ln -vsnf "$path" instruments.csv

# delete the temp dir
if [ -d "${tmp_dir}" ]; then
    echo deleting temp-directory: ${tmp_dir}
    rm -rf "${tmp_dir}"
fi
