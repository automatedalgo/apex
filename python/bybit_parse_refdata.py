# Copyright 2024 Automated Algo (www.automatedalgo.com)

# This file is part of Automated Algo's "Apex" project.

# Apex is free software: you can redistribute it and/or modify it under the
# terms of the GNU Lesser General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option) any
# later version.

# Apex is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
# details.

# You should have received a copy of the GNU Lesser General Public License along
# with Apex. If not, see <https://www.gnu.org/licenses/>.

import argparse
from pathlib import Path
import json
import logging
import sys
from dataclasses import dataclass, asdict
from datetime import datetime as dt

import apex.logging

today_str = dt.today().strftime('%Y-%m-%d')

currency_map = dict() # will store map of all currencies

DEFAULT_PRECISION = 9


@dataclass
class AssetRefData:
    venue: str
    type: str
    instId: str
    symbol: str
    feed_symbol: str
    line_symbol: str
    baseAsset: str
    quoteAsset: str
    tickSize: float
    lotQty: float
    status: str
    quoteAssetPrecision: int
    baseAssetPrecision: int
    minQty: float
    minNotional: float
    recordDate: str


log_warn_once_msgs = set()
def log_warn_once(msg):
    if msg not in log_warn_once_msgs:
        log_warn_once_msgs.add(msg)
        logging.warning(msg)



def parse_fut_symbols(fut_symbols_raw, venue):

    assert fut_symbols_raw["retCode"] == 0
    data = fut_symbols_raw["result"]["list"]
    assert isinstance(data, list)

    items = []
    for item in data:

        # is_open = item["status"] == "Open"
        # if not is_open:
        #     continue
        base_ccy = item["baseCoin"]
        quote_ccy = item["quoteCoin"]
        native_symbol = item["symbol"]
        norm_base_ccy = base_ccy
        norm_quote_ccy = quote_ccy
        if norm_base_ccy is None:
            logging.info(f"skipping {native_symbol} - native base currency '{base_ccy}' not in spot list")
            continue
        if norm_quote_ccy is None:
            logging.info(f"skipping {native_symbol} - native quote currency '{quote_ccy}' not in spot list")
            continue

        asset_id_root = "{}/{}".format(norm_base_ccy,norm_quote_ccy)
        inst_id = f"{asset_id_root}.PF.BBT"

        # include only the perps for now
        if item["contractType"] != "LinearPerpetual":
            continue

        # TODO: add a description column?
        refitem = AssetRefData(
            venue=venue,
            type="perp",
            baseAsset=norm_base_ccy,
            quoteAsset=norm_quote_ccy,
            instId=inst_id,
            symbol=native_symbol,
            feed_symbol=native_symbol,
            line_symbol=native_symbol,
            tickSize=item["priceFilter"]["tickSize"],
            lotQty=item["lotSizeFilter"]["qtyStep"],
            status=item["status"],
            quoteAssetPrecision=DEFAULT_PRECISION,
            baseAssetPrecision=DEFAULT_PRECISION,
            minQty=item["lotSizeFilter"]["minOrderQty"],
            minNotional=item["lotSizeFilter"]["minNotionalValue"],
            recordDate=today_str
        )
        items.append(refitem)
    return [asdict(x) for x in items]


def extract_header_cols(rows, index_field):
    header_cols = [index_field]
    header_cols_unique = set(index_field)
    for row in rows:
        for key in row.keys():
            if key not in header_cols_unique:
                header_cols.append(key)
                header_cols_unique = set(header_cols)
    return header_cols


# TODO: possibly detect and handle comma found within field values
def write_csv_file(fn, rows, index_field, delim=','):

    header_cols = extract_header_cols(rows, index_field)

    # rows, indexed by rowId
    lines: Dict[str, List] = {}

    # generate row contents
    for row in rows:
        row_id = row[index_field]
        line = []
        for col in header_cols:
            val = row.get(col, "")
            line.append(val)
        if row_id in lines:
            print("ignoring duplicate row for '{}'".format(row_id))
            print(row)
        else:
            lines[row_id] = line  # TODO: chcekc duplicated

    logging.info(f"writing file '{fn}'")
    with open(fn, "w") as f:
        f.write(delim.join(header_cols))
        f.write("\n")
        for key in sorted(lines.keys()):
            f.write(delim.join([str(x) for x in lines[key]]))
            f.write("\n")


def load_json(filename):
    logging.info(f"reading '{filename}'")
    with open(filename, "r") as f:
        return json.load(f)


def parse_args():
    parser = argparse.ArgumentParser(description="Program with temp directory option")
    parser.add_argument(
        "--tmp",
        type=Path,
        default=Path("default-tmp"),
        help="Path to temporary directory"
    )
    return parser.parse_args()


def main():
    apex.logging.init_logging()

    args = parse_args()
    tmp_dir = args.tmp

    # load the futures trading symbols and parse
    fut_symbols = load_json(f"{tmp_dir}/bybit_linear_currencies.json")

    futures = parse_fut_symbols(fut_symbols, venue="bybit")


    outfn = f"{tmp_dir}/bybit_assets.csv"
    all_rows = [*futures]
    write_csv_file(outfn, all_rows, "instId")


if __name__ == "__main__":
    main()
