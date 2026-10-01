/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/cpu_freq/cpu_freq.h>
#include <zephyr/cpu_freq/pstate.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#include "clock_control/clock_stm32_ll_common.h"

#define IC1_NODE DT_NODELABEL(ic1)
#define CPU_PLL_NODE DT_NODELABEL(CONCAT(pll, DT_PROP(IC1_NODE, pll_src)))
#define CPU_PLL_SOURCE DT_CLOCKS_CTLR(CPU_PLL_NODE)
#define CPU_BOOT_HZ DT_PROP(DT_NODELABEL(cpusw), clock_frequency)
#define CPU_PLL_NUMERATOR \
	((uint64_t)DT_PROP(CPU_PLL_SOURCE, clock_frequency) * DT_PROP(CPU_PLL_NODE, mul_n))
#define CPU_PLL_DENOMINATOR \
	((uint64_t)DT_PROP(CPU_PLL_NODE, div_m) * DT_PROP(CPU_PLL_NODE, div_p1) * \
	 DT_PROP(CPU_PLL_NODE, div_p2) * DT_PROP_OR(CPU_PLL_SOURCE, hsi_div, 1))
#define CPU_PLL_HZ (CPU_PLL_NUMERATOR / CPU_PLL_DENOMINATOR)

BUILD_ASSERT(DT_NODE_HAS_STATUS(CPU_PLL_NODE, okay), "CPU PLL must be enabled");
BUILD_ASSERT(DT_SAME_NODE(CPU_PLL_SOURCE, DT_NODELABEL(clk_hsi)) ||
	     DT_SAME_NODE(CPU_PLL_SOURCE, DT_NODELABEL(clk_hse)),
	     "CPU scaling requires an HSI or HSE PLL source");
BUILD_ASSERT(CPU_PLL_DENOMINATOR > 0U && CPU_PLL_NUMERATOR % CPU_PLL_DENOMINATOR == 0U,
	     "CPU PLL must have an exact integer frequency");
BUILD_ASSERT(DT_PROP(IC1_NODE, ic_div) >= 1U && DT_PROP(IC1_NODE, ic_div) <= 256U,
	     "Boot IC1 divider is out of range");
BUILD_ASSERT(CPU_BOOT_HZ > 0U && CPU_BOOT_HZ <= 800000000U &&
	     CPU_PLL_HZ == (uint64_t)CPU_BOOT_HZ * DT_PROP(IC1_NODE, ic_div),
	     "Boot CPU frequency must match the fixed IC1 parent and divider");

/* Voltage remains at the boot setting; only the IC1 divider changes. */
struct stm32n6_pstate {
	uint32_t frequency;
};

#define DEFINE_STM32N6_PSTATE(node_id) \
	BUILD_ASSERT(DT_NODE_HAS_COMPAT(node_id, st_stm32n6_pstate), \
		     "STM32N6 requires st,stm32n6-pstate nodes"); \
	BUILD_ASSERT(DT_PROP(node_id, clock_frequency) > 0U && \
		     DT_PROP(node_id, clock_frequency) <= CPU_BOOT_HZ, \
		     "P-state frequency must not exceed the boot CPU frequency"); \
	BUILD_ASSERT(CPU_PLL_HZ % DT_PROP(node_id, clock_frequency) == 0U && \
		     CPU_PLL_HZ / DT_PROP(node_id, clock_frequency) >= 1U && \
		     CPU_PLL_HZ / DT_PROP(node_id, clock_frequency) <= 256U, \
		     "P-state requires an exact IC1 divider from 1 through 256"); \
	static const struct stm32n6_pstate CONCAT(stm32n6_pstate_, node_id) = { \
		.frequency = DT_PROP(node_id, clock_frequency), \
	}; \
	PSTATE_DT_DEFINE(node_id, &CONCAT(stm32n6_pstate_, node_id))

DT_FOREACH_CHILD_STATUS_OKAY(DT_PATH(performance_states), DEFINE_STM32N6_PSTATE)

int cpu_freq_pstate_set(const struct pstate *state)
{
	const struct stm32n6_pstate *cfg;

	if (state == NULL || state->config == NULL) {
		return -EINVAL;
	}

	cfg = state->config;
	return stm32_clock_control_set_cpu_rate(cfg->frequency);
}
