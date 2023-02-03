#!/usr/bin/env bash
# Copyright 2022 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

set -eu
bash_source_dir="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"

source "${bash_source_dir}/_env.sh"
simulation_id="${test_name}"
verbosity_level=2
process_ids=""
exit_code=0

: "${BSIM_OUT_PATH:?BSIM_OUT_PATH must be defined}"

cd ${BSIM_OUT_PATH}/bin

"$test_exe" -v=${verbosity_level} -s="${simulation_id}" -d=0 -testid=server -RealEncryption=1 -argstest 0 2 "server" &
"$test_exe" -v=${verbosity_level} -s="${simulation_id}" -d=1 -testid=server -RealEncryption=1 -argstest 1 2 "server" &

"$test_exe" -v=${verbosity_level} -s="${simulation_id}" -d=2 -testid=client -RealEncryption=1 -argstest 0 0 "client" &

./bs_2G4_phy_v1 -v=${verbosity_level} -s="${simulation_id}" -D=3 -sim_length=100e6

# Remove the files used by the custom SETTINGS backend
echo "remove settings files ${simulation_id}_*.log"
cat ${simulation_id}_*.log
rm ${simulation_id}_*.log

exit $exit_code #the last exit code != 0
