#!/usr/bin/env bash
# Copyright 2023 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0
set -eu
: "${ZEPHYR_BASE:?ZEPHYR_BASE must be defined}"

INCR_BUILD=1

export BOARD="${BOARD:-nrf5340bsim/nrf5340/cpuapp}"

source ${ZEPHYR_BASE}/tests/bsim/compile.source

app="$(guess_test_relpath)" sysbuild=1 compile

wait_for_background_jobs
