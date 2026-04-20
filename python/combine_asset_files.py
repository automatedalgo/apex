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
import pandas as pd

import apex.logging

def combine_files(filenames: list[Path]):
    if len(filenames) == 0:
        raise Exception("list of files is empty")
    dataframes = []
    for filename in filenames:
        logging.info(f"reading '{filename}'")
        dataframes.append(pd.read_csv(filename))
    final = pd.concat(dataframes)
    return final


def parse_args():
    parser = argparse.ArgumentParser(description="Program with temp directory option")
    parser.add_argument(
        "--tmp",
        type=Path,
        default=Path("default-tmp"),
        help="Path to temporary directory"
    )
    parser.add_argument(
        "files",
        nargs="*",   # use "+" if you want to require at least one file
        type=Path,
        help="Input files to process"
    )
    return parser.parse_args()


def main():
    apex.logging.init_logging()

    args = parse_args()
    tmp_dir = args.tmp

    combined = combine_files([x for x in args.files])

    # check the instId is unique
    final_df = combined.set_index("instId", verify_integrity=True).sort_index()

    final_file = tmp_dir/"instruments.csv"
    logging.info(f"writing final file to '{final_file}'")

    final_df.to_csv(final_file, float_format="%.8f")


if __name__ == "__main__":
    main()
