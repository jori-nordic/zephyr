#!/usr/bin/env bash
# Copyright 2022 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

# Usage:
# one script instance per device, e.g. to run gdb on the client:
# `_notify-debug.sh client debug`
# `_notify-debug.sh server`
# `_notify-debug.sh`
#
# GDB can be run on the two devices at the same time without issues, just append
# `debug` when running the script.


simulation_id="l2cap_ecred"
verbosity_level=2
process_ids=""; exit_code=0

: "${BSIM_OUT_PATH:?BSIM_OUT_PATH must be defined}"

#Give a default value to BOARD if it does not have one yet:
BOARD="${BOARD:-nrf52_bsim}"

cd ${BSIM_OUT_PATH}/bin

if [[ $2 == "debug" ]]; then
  GDB_P="gdb --args "
fi

if [[ $1 == "central" ]]; then
$GDB_P ./bs_${BOARD}_tests_bluetooth_bsim_bt_bsim_test_l2cap_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=0 -testid=central -rs=43

elif [[ $1 == "peripheral" ]]; then
$GDB_P ./bs_${BOARD}_tests_bluetooth_bsim_bt_bsim_test_l2cap_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=1 -testid=peripheral -rs=42

else
./bs_2G4_phy_v1 -v=${verbosity_level} -s=${simulation_id} \
  -D=2 -sim_length=60e6 $@

fi
