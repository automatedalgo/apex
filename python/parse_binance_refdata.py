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

import json
import logging
import sys

import apex.logging


log_warn_once_msgs = set()
def log_warn_once(msg):
    if msg not in log_warn_once_msgs:
        log_warn_once_msgs.add(msg)
        logging.warning(msg)


def parse_binance_spot_exchange_info(fn):
    venue = "binance"
    assetType = "coinpair"
    logging.info("reading file '{}'".format(fn))
    # read json
    with open(fn) as f:
        data = json.load(f)
    symbols = data["symbols"]
    logging.info("file has {} symbols".format(len(symbols)))
    rows = []

    filters_to_ignore = set(
        ['MAX_NUM_ALGO_ORDERS',
         'ICEBERG_PARTS',
         'MARKET_LOT_SIZE',
         'PERCENT_PRICE',
         'TRAILING_DELTA',
         'PERCENT_PRICE_BY_SIDE',
         'MAX_POSITION'
        ])

    logging.info("ignoring following filters: {}".format(filters_to_ignore))
    for item in symbols:
        asset_id_root = "{}/{}".format(item['baseAsset'], item['quoteAsset'])
        row = dict()
        row["symbol"] = item["symbol"]
        row["instId"] = asset_id_root + ".BNC"
        row["type"] = assetType
        row["venue"] = venue
        row["baseAsset"] = item["baseAsset"]
        row["quoteAsset"] = item["quoteAsset"]
        row["quoteAsset"] = item["quoteAsset"]
        row["quoteAssetPrecision"] = item["quoteAssetPrecision"]
        row["baseAssetPrecision"] = item["baseAssetPrecision"]
        row["status"] = item["status"]

        # TODO: here I am not supporting the MARKET_LOT_SIZE filter
        for filter in item['filters'] :
            filterType = filter['filterType']
            if filterType == 'LOT_SIZE':
                row["minQty"] = filter['minQty']
                row["maxQty"] = filter['maxQty']
                row["lotQty"] = filter['stepSize']
            elif filterType == 'MIN_NOTIONAL':
                row['minNotional'] = filter['minNotional']
            elif filterType == 'PRICE_FILTER':
                row['tickSize'] = filter['tickSize']
            elif filterType == "MAX_NUM_ORDERS":
                row['maxNumOrders'] = filter['maxNumOrders']
            elif filterType in filters_to_ignore:
                pass
            else:
                log_warn_once("ignoring binance filter '{}'".format(filterType))
        rows.append(row)

    return rows


def normalise_contractType(contractType: str) -> str:
    if (contractType == "PERPETUAL"):
        return "perp", "PF"
    if (contractType in ["CURRENT_QUARTER", "NEXT_QUARTER"]):
        return "future", ""
    print(f"unhandled contract type: {contractType}")
    return None, None


def simplify_future_native_code(symbol):
    month_codes = "FGHJKMNQUVXZ" # CME convention
    assert len(month_codes) == 12

    parts = symbol.split("_")
    if len(parts) != 2:
        raise Exception(f"expected symbol to split into 2 parts, '{symbol}'")
    if len(parts[1]) != 6:
        raise Exception(f"expected symbol-date to have len 6, '{parts[1]}'")

    year = parts[1][0:2]
    month = int(parts[1][2:4])
    month_code = month_codes[month-1]
    return ".".join([month_code+year[1],'BNC'])


def parse_binance_usdfut_exchange_info(fn, venue):
    logging.info("reading file '{}'".format(fn))
    # read json
    with open(fn) as f:
        data = json.load(f)
    symbols = data["symbols"]
    logging.info("file has {} symbols".format(len(symbols)))
    rows = []

    filters_to_ignore = set(
        ['MAX_NUM_ALGO_ORDERS',
         'ICEBERG_PARTS',
         'MARKET_LOT_SIZE',
         'PERCENT_PRICE'   # <=== could be useful to enable
        ])

    logging.info("ignoring following filters: {}".format(filters_to_ignore))
    for item in symbols:
        row = dict()
        asset_id_root = "{}/{}".format(item['baseAsset'], item['quoteAsset'])
        symbol = item["symbol"]
        row["symbol"] = symbol

        assetType, assetShortType = normalise_contractType(item['contractType'])
        if (assetType is None):
            logging.info(f"skipping asset '{symbol}', assetType is None")
            continue

        row["instId"] = asset_id_root
        if assetType == "perp" and not symbol.endswith("_PERP"):
            row["instId"] = f"{asset_id_root}.PF.BNC"
        elif assetType == "perp" and symbol.endswith(".PERP"):
            row["instId"] = symbol.replace("_PERP",".PF") + ".BNC"
        else:
            row["instId"] = f"{asset_id_root}.BNC"

        if assetType == "future":
            row["instId"] = "{}.{}".format(asset_id_root,
                                            simplify_future_native_code(symbol))


        row["type"] = assetType
        row["venue"] = venue
        row["baseAsset"] = item["baseAsset"]
        row["quoteAsset"] = item["quoteAsset"]
        row["marginAsset"] = item["marginAsset"]
        row["quoteAssetPrecision"] = item["quotePrecision"]
        row["baseAssetPrecision"] = item["baseAssetPrecision"]

        if "status" in item:
            row["status"] = item["status"]
        elif "contractStatus" in item:
            row["status"] = item["contractStatus"]
        else:
            row["status"] = "unknown"

        row["underlyingType"] = item.get("underlyingType", None)
        row["contractType"] = item.get("contractType", None)

        # TODO: here I am not supporting the MARKET_LOT_SIZE filter
        for filter in item['filters'] :
            filterType = filter['filterType']
            if filterType == 'LOT_SIZE':
                row["minQty"] = filter['minQty']
                row["maxQty"] = filter['maxQty']
                row["lotQty"] = filter['stepSize']
            elif filterType == 'MIN_NOTIONAL':
                row['minNotional'] = filter['notional']
            elif filterType == 'PRICE_FILTER':
                row['tickSize'] = filter['tickSize']
            elif filterType == "MAX_NUM_ORDERS":
                row['maxNumOrders'] = filter['limit']
            elif filterType in filters_to_ignore:
                pass
            else:
                logging.warn("ignoring binance filter '{}'".format(filterType))

        rows.append(row)

    return rows


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
        else:
            lines[row_id] = line  # TODO: chcekc duplicated

    logging.info(f"writing file '{fn}'")
    with open(fn, "w") as f:
        f.write(delim.join(header_cols))
        f.write("\n")
        for key in sorted(lines.keys()):
            f.write(delim.join([str(x) for x in lines[key]]))
            f.write("\n")


def main():
    apex.logging.init_logging()

    fn = "tmp/binance_exchange-info.json"
    coin_rows = parse_binance_spot_exchange_info(fn)

    fn = "tmp/binance_usdfut_exchange-info.json"
    futures_rows = parse_binance_usdfut_exchange_info(fn,
                                                      venue="binance_usdfut")

    fn = "tmp/binance_coinfut_exchange-info.json"
    coinfut_rows = parse_binance_usdfut_exchange_info(fn,
                                                      venue="binance_coinfut")

    outfn = "tmp/binance_assets.csv"
    all_rows = [*coin_rows, *futures_rows, *coinfut_rows]
    write_csv_file(outfn, all_rows, "instId")


if __name__ == "__main__":
    main()
