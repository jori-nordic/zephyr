# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

include(FetchContent)
FetchContent_Declare(
  tracy
  GIT_REPOSITORY https://github.com/wolfpld/tracy.git
  GIT_TAG 813f4243db8e3e601860c213b8826dbf232fa891
  GIT_SHALLOW TRUE
  GIT_PROGRESS TRUE
)

set(TRACY_ENABLE "" ON)
set(TRACY_NO_EXIT "" ON)
set(TRACY_DELAYED_INIT "" OFF)
set(TRACY_STATIC "" OFF)

FetchContent_MakeAvailable(tracy)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

zephyr_link_libraries(TracyClient)
set_target_properties(TracyClient PROPERTIES INTERFACE_LINK_LIBRARIES "-Wl,--whole-archive $<TARGET_FILE:TracyClient> -Wl,--no-whole-archive")

# FIXME: maybe the cmake options are enough
target_compile_definitions(app PUBLIC TRACY_ENABLE=1)
target_compile_definitions(app PUBLIC TRACY_NO_EXIT=1)

project(bsim_test_sequential_dut)

target_sources(app PRIVATE
  src/main.c
)

zephyr_include_directories(
  ../common/
  ${BSIM_COMPONENTS_PATH}/libUtilv1/src/
  ${BSIM_COMPONENTS_PATH}/libPhyComv1/src/
)
