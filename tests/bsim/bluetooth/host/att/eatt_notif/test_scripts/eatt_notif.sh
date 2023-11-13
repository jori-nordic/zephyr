#!/usr/bin/env bash
# Copyright 2022 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

# EATT notification reliability test

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

simulation_id="eatt_notif"
verbosity_level=2
EXECUTE_TIMEOUT=120

trace_filepath="$(pwd)/trace_output/ctf_trace"
mkdir -p $(pwd)/trace_output

bsim_exe=bs_nrf52_bsim_tests_bsim_bluetooth_host_att_eatt_notif_prj_conf
west build -b nrf52_bsim && \
    cp build/zephyr/zephyr.exe ${BSIM_OUT_PATH}/bin/${bsim_exe} || false

cd ${BSIM_OUT_PATH}/bin

Execute ./bs_${BOARD}_tests_bsim_bluetooth_host_att_eatt_notif_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=0 -testid=client -trace-file=${trace_filepath}_0

Execute ./bs_${BOARD}_tests_bsim_bluetooth_host_att_eatt_notif_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=1 -testid=server -trace-file=${trace_filepath}_1

Execute ./bs_2G4_phy_v1 -v=${verbosity_level} -s=${simulation_id} \
  -D=2 -sim_length=60e6 $@

wait_for_background_jobs
