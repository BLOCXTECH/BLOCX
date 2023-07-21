#!/usr/bin/env bash
# use testnet settings,  if you need mainnet,  use ~/.blocx/blocxd.pid file instead
export LC_ALL=C

blocx_pid=$(<~/.blocx/testnet3/blocxd.pid)
sudo gdb -batch -ex "source debug.gdb" blocxd ${blocx_pid}
