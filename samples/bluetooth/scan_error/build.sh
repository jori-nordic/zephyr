#!/usr/bin/env bash

# We don't want to execute if we have a build error
set -eu

SCANNER_DIR=$(west topdir)/zephyr/samples/bluetooth/scan_error
ADVERTISER_DIR=$(west topdir)/zephyr/samples/bluetooth/broadcaster

# Build the scanner
pushd ${SCANNER_DIR}
west build -b nrf52_bsim
popd

# Build the advertiser
pushd ${ADVERTISER_DIR}
west build -b nrf52_bsim
popd
