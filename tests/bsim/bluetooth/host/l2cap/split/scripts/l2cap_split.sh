#!/usr/bin/env bash


BOARD="${BOARD:-nrf52_bsim}"
dut_exe="bs_${BOARD}_tests_bsim_bluetooth_host_l2cap_split_dut_prj_conf"
tester_exe="bs_${BOARD}_tests_bsim_bluetooth_host_l2cap_split_tester_prj_conf"

# terminate running simulations (if any)
${BSIM_COMPONENTS_PATH}/common/stop_bsim.sh

west build -b ${BOARD} -d build_dut dut && \
    cp build_dut/zephyr/zephyr.exe "${BSIM_OUT_PATH}/bin/${dut_exe}" &&
west build -b ${BOARD} -d build_tester tester && \
    cp build_tester/zephyr/zephyr.exe "${BSIM_OUT_PATH}/bin/${tester_exe}"

if [[ $? -ne 0 ]]; then
    exit 1
fi

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

test_name="l2cap_split"
simulation_id="${test_name}"
verbosity_level=2
EXECUTE_TIMEOUT=30
sim_length_us=30e6

cd ${BSIM_OUT_PATH}/bin

Execute ./bs_2G4_phy_v1 -v=${verbosity_level} -s="${simulation_id}" -D=2 -sim_length=${sim_length_us} $@
Execute "./$dut_exe" -v=${verbosity_level} -s="${simulation_id}" -d=0 -testid=test_0 -RealEncryption=1
Execute "valgrind ./$tester_exe" -v=${verbosity_level} -s="${simulation_id}" -d=1 -testid=test_0 -RealEncryption=1

wait_for_background_jobs
