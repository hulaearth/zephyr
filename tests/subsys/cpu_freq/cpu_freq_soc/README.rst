.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

STM32N6 reference configuration
##############################

The ``subsys.cpu_freq.soc.stm32n6`` scenario uses
``nucleo_n657x0_q/stm32n657xx/sb`` or ``stm32n6570_dk/stm32n657xx/sb``.
``pstate-stm32n6.overlay`` sets a 600 MHz CPU boot rate, IC1 divider four,
and P-states at 600, 200, 50 and 16 MHz. The scenario explicitly selects
pressure policy and LSE-backed LPTIM1 timekeeping. ``prj.conf`` delays policy
evaluation beyond the test duration so it cannot change the measured state.

Build only:

.. code-block:: console

   west twister -p nucleo_n657x0_q/stm32n657xx/sb \
     -T tests/subsys/cpu_freq/cpu_freq_soc \
     -s subsys.cpu_freq.soc.stm32n6 --build-only

Before execution, verify the fixed VDDCORE supply, regulator mode, voltage
readiness and HSE startup prerequisites described in
``tests/drivers/clock_control/stm32n6_cpu_clock/README.rst``. This scenario
adds no power-startup support. With those prerequisites verified and the
board in serial boot mode, use STM32CubeProgrammer and local USB DFU and
console ports:

.. code-block:: console

   west twister -p nucleo_n657x0_q/stm32n657xx/sb \
     -T tests/subsys/cpu_freq/cpu_freq_soc \
     -s subsys.cpu_freq.soc.stm32n6 --device-testing \
     --device-serial /dev/ttyACM_CONSOLE --west-runner stm32cubeprogrammer \
     --west-flash="--port=usbN"

The test checks backend requests, CPU metadata and fixed timer frequency.
The benchmark runs a bounded integer workload with timer interrupts enabled
on STM32N6, including at 16 MHz, and restores the highest P-state. Its elapsed
time includes interrupt overhead; it is not an independent physical CPU-rate
or energy measurement. The 32-bit elapsed-time subtraction requires the
measurement to last less than one full counter period (over 36 hours at
32768 Hz). No hardware result is implied by a successful build.
