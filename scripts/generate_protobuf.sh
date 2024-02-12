#!/usr/bin/env bash

set -eu

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

source "${SCRIPT_DIR}/depends.env"

${DEPS_DIR?}/protobuf-${PROTOBUF_VER?}/bin/protoc -I$SCRIPT_DIR/.. --cpp_out $SCRIPT_DIR/../src/apex/comm GxWireFormat.proto
