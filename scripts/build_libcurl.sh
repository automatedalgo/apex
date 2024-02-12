#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

source "${SCRIPT_DIR}/depends.env"
set -u

package=curl
version=${CURL_VER}

INSTALL_DIR=${DEPS_DIR}

TMP_DIR=/var/tmp/$(whoami)-deps/${package}-${version}
mkdir -p "$TMP_DIR"

cd "$TMP_DIR"
tarfile=curl-${version}.tar.gz
if [ ! -e ${tarfile} ];
then
    version_under=$(echo $version | sed s/\\./_/g)
    wget https://github.com/curl/curl/releases/download/curl-"$version_under"/$tarfile
else
    echo source distribution file already downloaded
fi

test -d build && rm -rf build
mkdir build

tar xfvz $tarfile

cd "$TMP_DIR"/build
"$TMP_DIR"/${package}-${version}/configure --prefix=${DEPS_DIR}/${package}-${version} \
          --with-openssl

nice make -j 4
make install
