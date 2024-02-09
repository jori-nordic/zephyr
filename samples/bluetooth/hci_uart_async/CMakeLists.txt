# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(EXTRA_DTC_OVERLAY_FILE app.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(hci_uart_async)
target_sources(app PRIVATE
        src/hci_uart_async.c
        src/main.c
)

