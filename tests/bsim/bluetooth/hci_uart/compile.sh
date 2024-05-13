#!/usr/bin/env bash
# Copyright 2024 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

# Compile all the applications needed by the bsim tests in these subfolders

#set -x #uncomment this line for debugging
set -ue
: "${ZEPHYR_BASE:?ZEPHYR_BASE must be set to point to the zephyr root directory}"

source ${ZEPHYR_BASE}/tests/bsim/compile.source

BOARD=native_sim/native/64
app=tests/bsim/bluetooth/ll/conn conf_file=prj_split_hci_uart.conf compile

# BOARD=nrf52_bsim
# app=samples/bluetooth/hci_uart compile
# app=samples/bluetooth/hci_uart_async compile

wait_for_background_jobs
