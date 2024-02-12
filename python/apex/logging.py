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

import logging
import os
import sys


def log_script_start(logger=logging.getLogger()):
    logger.info(
        "======================================================================"
    )
    logger.info("bin : {} ".format(os.path.basename(sys.argv[0])))
    logger.info("args: {} ".format(sys.argv[1:]))
    logger.info("cwd : {} ".format(os.getcwd()))
    logger.info("pid : {} ".format(os.getpid()))
    logger.info("ppid: {} ".format(os.getppid()))
    logger.info(
        "======================================================================"
    )


def init_logging(debug=False, preamble=True):
    logger = logging.getLogger()
    if debug:
        logger.setLevel(logging.DEBUG)
    else:
        logger.setLevel(logging.INFO)

    ch = logging.StreamHandler(sys.stdout)
    formatter = logging.Formatter(
        "%(asctime)s.%(msecs)03d | %(levelname)s | %(message)s", "%Y-%m-%d %H:%M:%S"
    )
    ch.setFormatter(formatter)
    logger.addHandler(ch)
    if preamble:
        log_script_start()
