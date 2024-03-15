.. _bluetooth_testing:

Bluetooth Testing
#################

In order to increase code quality, the Bluetooth stack is subjected to automated testing.
Some of them are run automatically on code changes that touch the Bluetooth subsystem.

There are three types of tests in the tree:
- Unit-tests
- Target tests
- Babblesim tests

Unit testing
************

Unit-tests are designed to verify individual "units" of code.
Those units can be a code block, a function or a bigger module.

They run on the same machine that compiles them, and do not include the kernel.
Only the specific unit is compiled, alongside so-called "mocks" or "stubs".

`the official docs <https://github.com/EDTTool/EDTT/blob/main/docs/EDTT_framework_Babblesim.md>`_

TODO: Add links for UT definition, mocks, stubs, FFF framework, Ztest fw

Those mocks/stubs are necessary, as the unit in question has dependencies on the rest of the system.

These mocks/stubs are designed to implement the minimum viable functionality of these dependencies,
in order to allow the unit to run (stubs) or to trigger certain behavior in the unit (mocks).
There is a third type, called "spies** that are used to inspect the side-effects of the unit.

TODO: include sample bluetooth UT using FFF.

Target testing
**************

Target-tests are designed to run on-target.
That is a hardware platform that the test is compiled for.
They can be qualified as system-tests, and include the whole kernel.

The majority of tests in Zephyr are of this type. They use the [ZTEST] framework, and are run by invoking [twister**.
Those test are run automatically by the Github CI pipeline on code changes.

TODO: example test + invocation.
TODO: twister filtering: individual test, tags, compile only, etc..
TODO: yaml explainer

There is a pretty big limitation: the framework was not designed with multiple devices in mind. E.g. a test involving two different devices, flashed with a different firmware.

That is a big limitation for Bluetooth, as the whole point of the protocol is communication between devices.

Babblesim testing
*****************

[Babblesim] is a framework for virtualizing an RF environment, and orchestrating multiple devices
interacting with that environment.

The Zephyr Bluetooth controller is compatible with that framework, as it also provides (simplified)
hardware models for the nordicsemi nRF chips.

A lot of Bluetooth subsystem tests are implemented using this framework.
They are also run in CI on code changes.

Tests implemented using this framework are also in the family of system-tests, as they test not only
the kernel, but also the whole communication chain between multiple devices. They however execute
very fast in most cases, enough to be in unit-test territory in that regard.

The framework is not opinionated, meaning we have to rely on conventions. Tests don't use [ztest]
and their execution is loosely defined. Similarly, they don't use [twister] as runner, rather ad-hoc
shell scripts.

Pros and cons
=============

The simulator's biggest strength is that it is both deterministic and fast.

In order to reach this goal, Babblesim will run code in "zero-time" from the Zephyr kernel's
perspective.

It will also fast-forward execution until the next event, e.g. `k_msleep(1000)` will not wait
for 1s, rather will return immediately, only advancing the kernel's uptime.

The hardware models also only hook on the HAL library functions, and busy-waiting doesn't work.

The debugging experience is stellar with Babblesim:
- very short execution speed -> fast iteration
- halting a device halts the whole simulation
- code size limits and processing overhead are virtually eliminated -> logging in critical sections is ok
- [rr] works! reverse execution dramatically reduces the speculation/hypothesis step

The flipside is that some behaviors are not testable, and some code has to be rewritten:
- Testing race conditions that arise from poorly-timed interrupts is not possible.
- Code touching HW registers directly has to be rewritten to use the HAL
- Busy-wait loops waiting for a HW interrupt have to be rewritten
- Code is compiled for x86 instead of the more common ARMv7 target

Conventions
===========

To describe:
- pass/fail criteria
- asserts
- folder structure
- compilation (incremental**
- runner script
- test suite + documentation

Tools
=====

- backchannels
- flash / settings
- testlib
- tinyhost
- useful kconfigs

Host tools
==========

- wireshark
- gdb
- rr

EDTT
****

EDTT tests are also system tests, and similar to Babblesim tests.
See `the official docs <https://github.com/EDTTool/EDTT/blob/main/docs/EDTT_framework_Babblesim.md>`_ for more info.

The big difference is that they compile a single image and define the tests in Python.
The python runner connects and is synced to the simulation in order to keep it deterministic.

Implemented in ``tests/bsim/bluetooth/ll/edtt``.
EDTT itself is typically cloned under ``[west topdir]/tools/edtt``.

Running the tests
=================

Assumes you already have babblesim configured (see above).

.. code-block:: bash

   # compile the tests (the whole ll folder)
   $ZEPHYR_BASE/tests/bsim/bluetooth/ll/compile.sh

   # Point scripts to EDTT
   export EDTT_PATH=$(west topdir)/tools/edtt

   # run the tests
   SEARCH_PATH=$ZEPHYR_BASE/tests/bsim/bluetooth/ll/edtt \
                $ZEPHYR_BASE/tests/bsim/run_parallel.sh
