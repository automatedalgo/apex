INSTALLATION
===========

This document describes how to build and install Apex on Linux systems.

Overview of steps:

1. System prerequisites
2. Obtain source code
3. Build & install external dependencies
4. Configure Apex source code
5. Build


System prerequisites
====================

To build and run Apex your Linux system will need to have essential C++ build
tools installed, including:

* git & wget
* gcc & python3
* cmake
* libssl
* automake, autoconf & libtool

Some Linux systems will come with these installed by default, however if they
are missing you will need to track down and install the required system
packages.

## Ubuntu

On Ubuntu 22 & 24 you can run the following commands to install essential build packages:

    sudo apt install git
    sudo apt install wget
    sudo apt install build-essential
    sudo apt install cmake
    sudo apt install libssl-dev
    sudo apt install zlib1g-dev
    sudo apt install automake
    sudo apt install libtool
    sudo apt install autoconf
    sudo apt install autotools-dev

## Redhat 8 / Centos 8

For Redhat/Centos 8 can try these:

    sudo dnf group install "Development Tools"
    sudo dnf install openssl-devel cmake3 git python3


Obtain source code
==================

Apex source code can be obtained from the github repo
[github.com/automatedalgo/apex](https://github.com/automatedalgo/apex).


Build external dependencies
===========================

Apex depends on several external open-source software projects. Each must be
compiled and made available to compile Apex.  Currently these dependencies are:

* protobuf3 (message format between Apex components)
* libcurl (used for REST requests)

Individual build scripts are provided to build each dependency. These can be
found under the `scripts` directory. To build all dependencies, issue the
following commands from the `apex` directory, one at a time:

    ./scripts/build_protobuf3.sh
    ./scripts/build_libcurl.sh

Each script performs a download, unzip, build and install. By default these
built dependencies are installed under `~/apex/deps`.

If any of these build scripts fail, then a likely possible cause is missing
system packages for building C/C++ software.

Configure & build
=================

Following successful install of the external dependencies, the next step is to
_configure_ the source code.  This involves running `cmake` to generate the
project build files.  Because running cmake takes several options, a wrapper
script is provided to assit.

    ./scripts/configure_cmake.sh debug

This creates a new directory named `BUILD-DEBUG`, into which the generated
makefiles are placed.  To now build Apex, just run make:

```shell
    cd BUILD-DEBUG
    nice make -j 2
    make install
```

If the build completes successfully, the complied binaries and libraries are
copied into `~/apex`.
