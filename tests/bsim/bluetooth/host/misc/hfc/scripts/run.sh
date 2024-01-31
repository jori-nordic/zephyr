#!/usr/bin/env bash
# Copyright (c) 2024 Nordic Semiconductor
# SPDX-License-Identifier: Apache-2.0

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

simulation_id="misc_hfc"
verbosity_level=2
EXECUTE_TIMEOUT=120

cd ${BSIM_OUT_PATH}/bin

bsim_exe=./bs_${BOARD}_tests_bsim_bluetooth_host_misc_hfc_prj_conf

Execute "${bsim_exe}" -v=${verbosity_level} -s=${simulation_id} -d=0 -testid=dut -rs=420
Execute "${bsim_exe}" -v=${verbosity_level} -s=${simulation_id} -d=1 -testid=peer_0 -rs=69

Execute ./bs_2G4_phy_v1 -v=${verbosity_level} -s=${simulation_id} -D=2 -sim_length=400e6 $@

wait_for_background_jobs
