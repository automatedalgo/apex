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
import pandas as pd

import apex.logging

def combine_files(filenames:list[str]):
    if len(filenames) == 0:
        raise Exception("list of files is empty")
    dataframes = []
    for filename in filenames:
        logging.info(f"reading '{filename}'")
        dataframes.append(pd.read_csv(filename))
    final = pd.concat(dataframes)
    return final

def main():
    combined = combine_files(sys.argv[1:])

    # check the instId is unique
    final_df = combined.set_index("instId", verify_integrity=True).sort_index()

    final_file = "tmp/instruments.csv"
    logging.info(f"writing final file to '{final_file}'")
    final_df.to_csv(final_file)




if __name__ == "__main__":
    apex.logging.init_logging()
    main()
