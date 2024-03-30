#!/usr/bin/env bash
#
# Copyright (c) 2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

export HOST=x86_64-w64-mingw32
export PACKAGES="python3 nsis g++-mingw-w64-x86-64 wine-binfmt wine64"
export DPKG_ADD_ARCH="i386"
export RUN_INTEGRATION_TESTS=false
export GOAL="deploy"
export BITCOIN_CONFIG="--enable-gui --enable-reduce-exports --disable-miner"
export DIRECT_WINE_EXEC_TESTS=true
