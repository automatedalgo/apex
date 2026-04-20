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

import apex.logging


spot_api = "https://api.binance.com"
spot_path = "/api/v3/exchangeInfo"

usdfut_api = "https://fapi.binance.com"
usdfut_path = "/fapi/v1/exchangeInfo"

coinfut_api = "https://dapi.binance.com"
coinfut_path = "/dapi/v1/exchangeInfo"


def perform_http_request(api, path):
    url = f"{api}{path}"
    logging.info("making HTTP GET request: {}".format(url))
    reply = requests.get(url, params=None)

    if reply.status_code != 200:
        raise Exception("http request failed: {}".format(reply.status_code))
    return reply.text

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

    reply = perform_http_request(usdfut_api, usdfut_path)
    fn = f"{tmp_dir}/binance_usdfut_exchange-info.json"
    logging.info("writing to file '{}'".format(fn))
    with open(fn, "w") as f:
        f.write(reply)

    reply = perform_http_request(coinfut_api, coinfut_path)
    fn = f"{tmp_dir}/binance_coinfut_exchange-info.json"
    logging.info("writing to file '{}'".format(fn))
    with open(fn, "w") as f:
        f.write(reply)

    reply = perform_http_request(spot_api, spot_path)
    fn = f"{tmp_dir}/binance_exchange-info.json"
    logging.info("writing to file '{}'".format(fn))
    with open(fn, "w") as f:
        f.write(reply)


if __name__ == "__main__":
    main()
