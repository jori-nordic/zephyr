#!/usr/bin/env bash

set -eu

base_dir=""

# Function to find prj.conf upwards
find_prj_conf() {
    local dir="$1"

    # Loop until reaching the root directory
    while [[ "$dir" != "" && "$dir" != "/" ]]; do
        # Check if prj.conf exists in the current directory
        if [[ -f "$dir/prj.conf" ]]; then
            echo "Found prj.conf at $dir"
            base_dir=$dir
            return 0
        fi
        # Move one directory up
        dir=$(dirname "$dir")
    done

    echo "prj.conf not found."
    return 1
}

# Function to find the first compile or build script recursively
find_compile_or_build_script() {
    local script_name=""

    echo "Looking in $base_dir"

    # Search recursively for scripts containing 'compile' or 'build'
    script_name=$(find "$base_dir" -type f \( -name '*compile*' -o -name '*build*' \) -executable | head -n 1)

    if [[ -z "$script_name" ]]; then
        echo "Compile or build script not found."
        return 1
    else
        echo "Found script $script_name"
        nice bash "$script_name"
        return 0
    fi
}

# Main script starts here
if [[ $# -eq 0 ]]; then
    echo "Usage: $0 <start_directory>"
    exit 1
fi

start_dir="$1"

# Find prj.conf upwards
find_prj_conf "$start_dir" || exit 1

# Assuming prj.conf was found in the current directory, start searching for compile/build script here
find_compile_or_build_script "$base_dir" || exit 1

exit 0
