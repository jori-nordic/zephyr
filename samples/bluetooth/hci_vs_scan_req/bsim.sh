#!/usr/bin/env bash

set -e

# Build scanner sample
pushd "$(west topdir)/zephyr/samples/bluetooth/observer"
west build -b nrf52_bsim
central="$(pwd)/build/zephyr/zephyr.exe"
popd

# Build sample
west build -b nrf52_bsim
per="$(pwd)/build/zephyr/zephyr.exe"

# Cleanup all existing sims
~/sdk/bsim/components/common/stop_bsim.sh

$central -s=myid -d=0 -RealEncryption=0 -rs=1000 &
$per -s=myid -d=1 -RealEncryption=0 -rs=70 &

# Start the PHY
pushd "${BSIM_OUT_PATH}/bin"
./bs_2G4_phy_v1 -s=myid -D=2 -sim_length=10e6
