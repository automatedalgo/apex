#!/usr/bin/env bash

set -eu

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

source "${SCRIPT_DIR}/depends.env"

echo "These are the options to proivde in Clion CMake options dialog:"
echo
echo "  -DLIBUV_DIR=${DEPS_DIR}/libuv-${LIBUV_VER}"
echo "  -DLIBCURL_DIR=${DEPS_DIR}/curl-${CURL_VER}"
echo "  -DLIBPROTOBUF3_DIR=${DEPS_DIR}/protobuf-${PROTOBUF_VER}"
echo
