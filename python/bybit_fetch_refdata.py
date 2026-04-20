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

bybit_url = "https://api.bybit.com"
bybit_url_alt = "https://api.bytick.com"

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

    # API documentation:
    #
    #   https://bybit-exchange.github.io/docs/v5/intro

    # TODO: support cursor, so that we can get more than 1000
    # currencies
    fetch_and_save(bybit_url,
                   "/v5/market/instruments-info?category=linear&limit=1000&status=Trading",
                   f"{tmp_dir}/bybit_linear_currencies.json")


if __name__ == "__main__":
    main()
