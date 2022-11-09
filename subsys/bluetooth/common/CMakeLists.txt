# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(addr.c)
zephyr_library_sources(dummy.c)
zephyr_library_sources(log.c)

zephyr_library_sources_ifdef(CONFIG_BT_RPA   rpa.c)

zephyr_library_link_libraries(subsys__bluetooth)

zephyr_compile_definitions_ifdef(CONFIG_BT_DEBUG_MONITOR_SWO ENABLE_SWO)
