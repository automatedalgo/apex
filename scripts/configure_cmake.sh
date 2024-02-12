#!/usr/bin/env bash

set -e   # no -u, because we process $1

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

source "${SCRIPT_DIR}/depends.env"


buildmode="$1"

# This 'deps' direction is where we install external dependencies required to
# build Apex.
INSTALL_DIR=$APEX_HOME

if [ "$buildmode" != "debug" -a "$buildmode" != "release" ];
then
    echo please specficy build profile, \'debug\' or \'release\'
    exit 1
fi

projectdir=$(pwd)

BUILD_DIR="${SCRIPT_DIR}/../BUILD-${buildmode^^}"

rm -rf "${BUILD_DIR}/"
mkdir -p "$_"
cd "$_"

set -x
cmake   \
  -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
  -DCMAKE_BUILD_TYPE=${buildmode^} \
  -DCMAKE_VERBOSE_MAKEFILE:BOOL=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=1 \
  -DLIBUV_DIR="${DEPS_DIR}/libuv-${LIBUV_VER}"  \
  -DLIBCURL_DIR="${DEPS_DIR}/curl-${CURL_VER}" \
  -DLIBPROTOBUF3_DIR="${DEPS_DIR}/protobuf-${PROTOBUF_VER}" \
  -S ${SCRIPT_DIR}/.. -B "$BUILD_DIR"
set +x

echo

echo "to build:"
echo
echo "    cd BUILD-${buildmode^^}; nice make -j 2"
