#!/usr/bin/env bash

# cd to:
# zephyr/samples/bluetooth/broadcast_audio_source
#
# run:
# west build -b nrf52_bsim && ./run-test.sh
#
${BSIM_COMPONENTS_PATH}/common/stop_bsim.sh

cp build/zephyr/zephyr.exe ${BSIM_OUT_PATH}/bin/iso.exe

cd ${BSIM_OUT_PATH}/bin

./iso.exe -s=iso -d=0 &
./bs_2G4_phy_v1 -s=iso -D=1 -sim_length=2e6
