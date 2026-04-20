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
import requests
import json
import logging
import sys
import json

import apex.logging


kucoin_url = "https://api.kucoin.com"
kucoin_fut_url = "https://api-futures.kucoin.com"

spot_exchid = "kucoin"
spot_url = "https://api.kucoin.com"
spot_path = "/api/v2/symbols"

fut_exchid = "kucoin_fut"
fut_url = "https://api-futures.kucoin.com"
fut_path = "/api/v1/contracts/active"

#coinfut_url = "https://dapi.binance.com"
#coinfut_path = "/dapi/v1/exchangeInfo"

import http.client

conn = http.client.HTTPSConnection("api-futures.kucoin.com")
payload = ''
headers = {}
conn.request("GET", "", payload, headers)
res = conn.getresponse()
data = res.read()
print(data.decode("utf-8"))


def perform_http_request(api, path):
    url = f"{api}{path}"
    logging.info("making HTTP GET request: {}".format(url))
    reply = requests.get(url, params=None)

    if reply.status_code != 200:
        raise Exception("http request failed: {}".format(reply.status_code))
    return reply.text

def fetch_and_save(api, path, filename):
    reply = perform_http_request(api, path)
    logging.info("writing to file '{}'".format(filename))
    with open(filename, "w") as f:
        as_json = json.loads(reply)
        f.write(json.dumps(as_json, indent=True))

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

    # currencies
    fetch_and_save(kucoin_url,
                   "/api/v3/currencies",
                   f"{tmp_dir}/kucoin_spot_currencies.json")

    # available spot currency pairs for trading
    fetch_and_save(kucoin_url,
                   "/api/v2/symbols",
                   f"{tmp_dir}/kucoin_spot_symbols.json")

    # available spot currency pairs for trading
    fetch_and_save(kucoin_fut_url,
                   "/api/v1/contracts/active",
                   f"{tmp_dir}/kucoin_fut_symbols.json")

if __name__ == "__main__":
    main()
