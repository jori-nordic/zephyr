#!/usr/bin/env bash
# Copyright (c) 2024 Nordic Semiconductor
# SPDX-License-Identifier: Apache-2.0

# [UK left the chat]
set -eu

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

test_name="$(guess_test_long_name)"

simulation_id=${test_name}

SIM_LEN_US=$((10 * 1000 * 1000))

test_exe="${BSIM_OUT_PATH}/bin/bs_${BOARD_TS}_${test_name}_prj_conf"

cd ${BSIM_OUT_PATH}/bin

Execute "${test_exe}" -s=${simulation_id} -d=0 -rs=420 -testid=dut
Execute "${test_exe}" -s=${simulation_id} -d=1 -rs=69  -testid=central
Execute "${test_exe}" -s=${simulation_id} -d=2 -rs=69  -testid=central

Execute ./bs_2G4_phy_v1 -s=${simulation_id} -D=3 -sim_length=${SIM_LEN_US} $@

wait_for_background_jobs
