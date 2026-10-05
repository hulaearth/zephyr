.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

STM32N6 CPU clock scaling test
#############################

The reference target is ``nucleo_n657x0_q/stm32n657xx/sb``. The same overlay
supports ``stm32n6570_dk/stm32n657xx/sb``. ``app.overlay`` keeps the board's
2.4 GHz PLL1 parent, sets IC1 to divide by four and boots the CPU at 600 MHz.
``prj.conf`` selects LPTIM1 with LSE at 32768 Hz and disables PM and SysTick.
Board defaults are unchanged.

Before hardware execution, verify the board's actual VDDCORE supply, regulator
mode and voltage readiness against the STM32N657 datasheet for the 600 MHz CPU
and unchanged bus clocks. The startup path must establish and wait for that
supply before selecting PLL1/IC1. Setting the internal regulator's voltage
scale alone does not establish readiness of an external supply. Also verify
HSE startup stability; HSERDY alone does not guarantee it (ES0620). This test
does not supply additional power-startup code. Until these prerequisites are
verified, use build-only validation. Do not substitute an 800 MHz profile.

Live IC1 divider changes also require confirmation against the applicable
reference manual. RM0486 Rev 2, PLL description, explicitly describes ICx
updates while disabled; its separate CPU/bus on-the-fly statement does not
explicitly cover IC1. The LL setter and register readback alone do not
establish this hardware guarantee. Hardware validation is pending that
confirmation.

Build the normal suite without executing it:

.. code-block:: console

   west twister -p nucleo_n657x0_q/stm32n657xx/sb \
     -T tests/drivers/clock_control/stm32n6_cpu_clock \
     -s drivers.clock_control.stm32n6_cpu_clock --build-only

After verifying the prerequisites, install STM32CubeProgrammer, put the board
in serial boot mode and connect its USB DFU and serial console ports. Execute
with the board's ``stm32cubeprogrammer`` runner (replace the local port names):

.. code-block:: console

   west twister -p nucleo_n657x0_q/stm32n657xx/sb \
     -T tests/drivers/clock_control/stm32n6_cpu_clock \
     -s drivers.clock_control.stm32n6_cpu_clock --device-testing \
     --device-serial /dev/ttyACM_CONSOLE --west-runner stm32cubeprogrammer \
     --west-flash="--port=usbN"

Select ``drivers.clock_control.stm32n6_cpu_clock.stress`` instead for 10,000
additional requests. In a worktree, export ``ZEPHYR_BASE`` to that worktree
before invoking west.

The suite checks invalid and same-rate requests, non-adjacent transitions
among 600, 200, 50 and 16 MHz, restoration of 600 MHz, and calls from a timer
ISR. It compares divider readback, reported rates and ``SystemCoreClock``,
checks that other clock settings are unchanged, and exercises kernel timing
with interrupts enabled. Register-derived rates are not independent physical
CPU-frequency measurements. These tests do not establish voltage margins,
energy savings, interrupt-disabled latency or external-memory reliability.
