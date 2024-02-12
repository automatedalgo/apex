#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

source "${SCRIPT_DIR}/depends.env"
set -u

version=${LIBUV_VER}

TMP_DIR=/var/tmp/$(whoami)-deps/libuv-${version}
mkdir -p "$TMP_DIR"

cd "$TMP_DIR"
tarfile=libuv-v${version}.tar.gz
if [ ! -e ${tarfile} ];
then
    wget https://dist.libuv.org/dist/v${version}/libuv-v${version}.tar.gz
else
    echo source distribution file already downloaded
fi

test -d build && rm -rf build
mkdir build

tar xfvz $tarfile

cd "$TMP_DIR"/libuv-v${version}
./autogen.sh

cd "$TMP_DIR"/build
"$TMP_DIR"/libuv-v${version}/configure --prefix=${DEPS_DIR}/libuv-${version}


nice make -j 2
make install
