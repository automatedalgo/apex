#!/usr/bin/env bash

set -e   # no -u, because we process $1

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

source "${SCRIPT_DIR}/depends.env"

echo "These are the options to proivde in Clion CMake options dialog:"
echo
echo "  -DLIBCURL_DIR=${DEPS_DIR}/curl-${CURL_VER}"
echo "  -DLIBPROTOBUF3_DIR=${DEPS_DIR}/protobuf-${PROTOBUF_VER}"
echo
