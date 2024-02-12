#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

source "${SCRIPT_DIR}/depends.env"
set -u

package=protobuf
version=${PROTOBUF_VER}

INSTALL_DIR=${DEPS_DIR}

TMP_DIR=/var/tmp/$(whoami)-deps/${package}-${version}
mkdir -p "$TMP_DIR"


cd "$TMP_DIR"
tarfile=protobuf-cpp-${version}.tar.gz
if [ ! -e ${tarfile} ];
then
    wget https://github.com/protocolbuffers/protobuf/releases/download/v${version}/$tarfile
else
    echo source distribution file already downloaded
fi

# if [ -e "${protobuf}${version}" ];
# then
#     rm -rf "protobuf-${version}"
# fi

test -d build && rm -rf build
mkdir build

tar xfvz $tarfile

export DIST_LANG=cpp
cd "$TMP_DIR"/build
"$TMP_DIR"/protobuf-${version}/configure --prefix=${DEPS_DIR}/${package}-${version}

nice make -j 2
make install
