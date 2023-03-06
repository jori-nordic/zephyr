#!/usr/bin/env bash
# Copyright 2023 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

# Place yourself in the test's root (i.e. ./../)
rm -rf ${BSIM_OUT_PATH}/bin/bs_nrf52_bsim_tests*

bsim_exe=bs_nrf52_bsim_tests_bsim_bluetooth_host_l2cap_stress_prj_conf

west build -b nrf52_bsim && \
    cp build/zephyr/zephyr.exe ${BSIM_OUT_PATH}/bin/${bsim_exe}
