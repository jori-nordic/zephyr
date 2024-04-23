#!/usr/bin/env bash

# We don't want to execute if we have a build error
set -eu

SCANNER_DIR=$(west topdir)/zephyr/samples/bluetooth/scan_error
ADVERTISER_DIR=$(west topdir)/zephyr/samples/bluetooth/broadcaster

# Cleanup all existing sims
~/sdk/bsim/components/common/stop_bsim.sh

# Start scanner device
# This is the one we're interested in
scanner="${SCANNER_DIR}/build/zephyr/zephyr.exe"
$scanner -s=myid -d=0 -RealEncryption=0 -rs=70 &

# Start advertiser device
advertiser="${ADVERTISER_DIR}/build/zephyr/zephyr.exe"
$advertiser -s=myid -d=1 -RealEncryption=0 -rs=90 &

# Run the PHY for 10 seconds
pushd "${BSIM_OUT_PATH}/bin"
./bs_2G4_phy_v1 -s=myid -D=2 -sim_length=10000000
