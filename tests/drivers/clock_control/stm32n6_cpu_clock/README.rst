.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

STM32N6 CPU clock scaling test
##############################

This hardware test changes the STM32N6 CPU clock while the kernel is running.
It requires the secure ``stm32n6570_dk`` or ``nucleo_n657x0_q`` board target,
with CPU clock source IC1, PLL1 at 2.4 GHz, and an 800 MHz CPU boot rate. The
fixed voltage configuration must support the 800 MHz boot rate. The default
STM32N6 board clock tree supplies these rates. Verify the actual VDDCORE
supply before running the 800 MHz configuration. A nominal-voltage lab
overlay may instead select a 600 MHz CPU boot rate with IC1 divider 4.

The normal suite checks invalid requests, same-rate idempotence, and a
non-adjacent rate sequence from 800 MHz through 16, 600, 50, and 200 MHz back
to 800 MHz. It checks IC1, the reported CPU rate, ``SystemCoreClock``, and the
fixed system timer frequency. The system timer uses LPTIM1 with a fixed LSE source; its
frequency stays unchanged when IC1 changes. Timer and uptime checks run with interrupts
enabled during rate changes. The final rate is restored to 800 MHz.

Run the suite on hardware with:

.. code-block:: console

   west twister -p stm32n6570_dk/stm32n657xx/sb \
     -T tests/drivers/clock_control/stm32n6_cpu_clock

The stress scenario performs 10,000 additional rate changes and is intended
for a connected board rather than routine CI:

.. code-block:: console

   west twister -p stm32n6570_dk/stm32n657xx/sb \
     -s tests/drivers/clock_control/stm32n6_cpu_clock/drivers.clock_control.stm32n6_cpu_clock.stress

The uptime and kernel timer checks show that system timing continues; they do
not independently measure CPU frequency. For a drift check, use a fixed-rate
reference such as LSE to gate DWT cycle-counter measurements at each requested
CPU rate, if DWT cycle counting is available on the board. The test does not
use MCO as a direct CPU clock output; the available MCO routes do not establish
that measurement path.
