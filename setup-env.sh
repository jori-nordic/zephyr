#!/usr/bin/env bash

set -eu

echo "Setting up environment"

git config --global user.email "bot@zephyrproject.org"
git config --global user.name "Zephyr Bot"
git log  --pretty=oneline | head -n 10

# TODO: skip if west already initialized
sudo chown -R user:user /workspaces
west init -l --mf west-bsim.yml || true
west config manifest.group-filter -- +ci
west config --global update.narrow true

west update --path-cache /repo-cache/zephyrproject 2>&1 1> west.update.log || west update --path-cache /repo-cache/zephyrproject 2>&1 1> west.update.log || ( rm -rf ../modules ../bootloader ../tools && west update --path-cache /repo-cache/zephyrproject)
# FIXME: don't do that for local dev. Actually, a different env-setup should be used.
west forall -c 'git reset --hard HEAD'

export ZEPHYR_SDK_INSTALL_DIR="/opt/toolchains/zephyr-sdk-$( cat SDK_VERSION )"
