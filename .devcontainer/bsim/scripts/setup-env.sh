#!/usr/bin/env bash

set -eu

echo "Setting up environment"

export ZEPHYR_SDK_INSTALL_DIR="/opt/toolchains/zephyr-sdk-$( cat SDK_VERSION )"

# Skip if a workspace already exists
config_path="/workspaces/.west/config"
if [ -f "$config_path" ]; then
    echo "West .config exists, skipping init and update."
    exit 0
fi

# Can that have bad consequences if host UID != 1000?
sudo chown user:user /workspaces

# Fetch all the projects we need
west init -l --mf .devcontainer/bsim/west-bsim.yml
west config --global update.narrow true
west update --path-cache /repo-cache/zephyrproject

# Reset every project except the main zephyr repo.
#
# I can't "just" use $(pwd) or shell expansion as it will be expanded _before_
# the command is executed in every west project.
west forall -c 'pwd | xargs basename | xargs test "zephyr" != && git reset --hard HEAD' || true
