#!/usr/bin/env bash

##
## if you are behind a firewall, set a proxy
##
##    export http_proxy=http://your_ip_proxy:port/
##    export https_proxy=$http_proxy
##


set -eu

##
## msgpack
##

# Need version >= 2.1.2, because for 2.1.2 and earlier, it has a bug in the
# decoding of msgpack buffers
ver=2.1.3
echo '***' fetching msgpack $ver '***'
echo
zipfile=cpp-${ver}.tar.gz
url=https://github.com/msgpack/msgpack-c/archive/$zipfile
test -f $zipfile || wget $url

if [ -f ${zipfile} ]; then
    tar xfz  ${zipfile}
    mv msgpack-c-cpp-${ver} msgpack-c
else
    echo failed to download msgpack ... please try manually
    exit
fi
