# Stop current sims
${BSIM_COMPONENTS_PATH}/common/stop_bsim.sh || 1

# Remove source
mkdir -p ./build_bsim/zephyr/
rm ./build_bsim/zephyr/zephyr.exe

# Compile source
west build -b nrf52_bsim --pristine auto --build-dir ./build_bsim

rm ${BSIM_OUT_PATH}/bin/bs_nrf52_bsim_build_l2cap_mesh
cp ./build_bsim/zephyr/zephyr.exe ${BSIM_OUT_PATH}/bin/bs_nrf52_bsim_build_l2cap_mesh

cd ${BSIM_OUT_PATH}/bin

# See https://babblesim.github.io/2G4_select_ch_mo.html
# -dist=/app/distances.matrix

# We run the simulation in the end so we can easily cancel it :)

SIM_NUM_DEVICES=$1

DEV_ID=22
SILENCE=0

for (( i=0; i < $SIM_NUM_DEVICES; ++i ))
do
  if [[ "$i" -eq $DEV_ID ]]; then
      echo "Skip $i"
      continue
  fi
  if [[ 1 -eq $SILENCE ]]; then
  ./bs_nrf52_bsim_build_l2cap_mesh -s=l2cap_mesh -d=$i > /dev/null 2>&1 &
  continue
  fi
      # gdb -q -batch -ex run -ex "thread apply all bt" --args ./bs_nrf52_bsim_build_l2cap_mesh -s=l2cap_mesh -d=$i &
  ./bs_nrf52_bsim_build_l2cap_mesh -s=l2cap_mesh -d=$i &
done

./bs_2G4_phy_v1 -s=l2cap_mesh -D=$(($SIM_NUM_DEVICES+0)) -sim_length=300e6 -defmodem=BLE_simple -channel=Indoorv1 -argschannel -preset=Huge3 -speed=1.1 -at=50 &

if [[ $DEV_ID -le $SIM_NUM_DEVICES ]]; then
gdb --args ./bs_nrf52_bsim_build_l2cap_mesh -s=l2cap_mesh -d=$DEV_ID
fi
