#!/usr/bin/env bash
# Copyright (c) 2022 Nordic Semiconductor
# SPDX-License-Identifier: Apache-2.0

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

# EATT test
simulation_id="l2cap_stress"
verbosity_level=2
EXECUTE_TIMEOUT=120

trace_filepath="$(pwd)/trace_output/ctf_trace"
mkdir -p $(pwd)/trace_output

cd ${BSIM_OUT_PATH}/bin

bsim_exe=./bs_${BOARD}_tests_bsim_bluetooth_host_l2cap_stress_prj_conf

Execute "${bsim_exe}" -v=${verbosity_level} -s=${simulation_id} -d=0 -testid=central -rs=43 -trace-file=${trace_filepath}_0

Execute "${bsim_exe}" -v=${verbosity_level} -s=${simulation_id} -d=1 -testid=peripheral -rs=42 -trace-file=${trace_filepath}_1
Execute "${bsim_exe}" -v=${verbosity_level} -s=${simulation_id} -d=2 -testid=peripheral -rs=10 -trace-file=${trace_filepath}_2
Execute "${bsim_exe}" -v=${verbosity_level} -s=${simulation_id} -d=3 -testid=peripheral -rs=23 -trace-file=${trace_filepath}_3
Execute "${bsim_exe}" -v=${verbosity_level} -s=${simulation_id} -d=4 -testid=peripheral -rs=7884 -trace-file=${trace_filepath}_4
Execute "${bsim_exe}" -v=${verbosity_level} -s=${simulation_id} -d=5 -testid=peripheral -rs=230 -trace-file=${trace_filepath}_5
Execute "${bsim_exe}" -v=${verbosity_level} -s=${simulation_id} -d=6 -testid=peripheral -rs=9 -trace-file=${trace_filepath}_6

Execute ./bs_2G4_phy_v1 -v=${verbosity_level} -s=${simulation_id} -D=7 -sim_length=400e6 $@

wait_for_background_jobs
