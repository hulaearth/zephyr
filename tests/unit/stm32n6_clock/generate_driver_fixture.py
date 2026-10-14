#!/usr/bin/env python3

# Copyright (c) 2026 Hula Firmware
#
# SPDX-License-Identifier: Apache-2.0

import pathlib
import sys


def extract_function(source: str, name: str) -> str:
	marker = f"{name}("
	search_from = 0
	while True:
		match = source.index(marker, search_from)
		start = source.rfind("\n", 0, match) + 1
		brace = source.find("{", match)
		semicolon = source.find(";", match)
		if semicolon < 0 or (brace >= 0 and brace < semicolon):
			break
		search_from = semicolon + 1
	depth = 0
	for end in range(brace, len(source)):
		if source[end] == "{":
			depth += 1
		elif source[end] == "}":
			depth -= 1
			if depth == 0:
				return source[start : end + 1]
	raise ValueError(f"Unclosed function {name}")


def main() -> None:
	if len(sys.argv) != 3:
		raise SystemExit("usage: generate_driver_fixture.py <source> <output>")

	source = pathlib.Path(sys.argv[1]).read_text()
	functions = [
		"get_msi_frequency",
		"get_pllsrc_frequency",
		"get_pllout_frequency",
		"get_icout_frequency",
		"get_sysclk_frequency",
		"stm32_clock_control_init",
	]
	fixture = r"""#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#define __unused __attribute__((unused))
#define __ASSERT(condition, ...) do { (void)(condition); } while (0)
#define __ASSERT_NO_MSG(condition) __ASSERT(condition, "assertion failed")
#define IS_ENABLED(option) (option)
#define MHZ(value) ((uint32_t)(value) * 1000000U)

#define PLL1_ID 1
#define PLL2_ID 2
#define PLL3_ID 3
#define PLL4_ID 4
#define STM32_PLL1_SRC_HSI 0
#define STM32_PLL1_SRC_HSE 0
#define STM32_PLL1_SRC_MSI 0
#define STM32_PLL_SRC_HSI 0
#define STM32_PLL_SRC_HSE 1
#define STM32_PLL_SRC_MSI 0
#define STM32_PLL2_SRC_HSI 0
#define STM32_PLL2_SRC_HSE 0
#define STM32_PLL2_SRC_MSI 0
#define STM32_PLL3_SRC_HSI 0
#define STM32_PLL3_SRC_HSE 0
#define STM32_PLL3_SRC_MSI 0
#define STM32_PLL4_SRC_HSI 0
#define STM32_PLL4_SRC_HSE 1
#define STM32_PLL4_SRC_MSI 0
#define STM32_PLL4_ENABLED 1
#define STM32_PLL4_M_DIVISOR 3
#define STM32_PLL4_N_MULTIPLIER 90
#define STM32_PLL4_P1_DIVISOR 2
#define STM32_PLL4_P2_DIVISOR 2
#define STM32_HSI_FREQ 64000000U
#define STM32_HSE_FREQ 32768000U
#define STM32_HSE_ENABLED 1
#define STM32_HSI_ENABLED 1
#define STM32_MSI_ENABLED 1
#define STM32_IC2_ENABLED 1
#define RCC_HSICFGR_HSIDIV_Pos 0
#define LL_RCC_ICCLKSOURCE_PLL1 1U
#define LL_RCC_ICCLKSOURCE_PLL2 2U
#define LL_RCC_ICCLKSOURCE_PLL3 3U
#define LL_RCC_ICCLKSOURCE_PLL4 4U
#define LL_RCC_SYS_CLKSOURCE_STATUS_HSE 1U
#define LL_RCC_SYS_CLKSOURCE_STATUS_HSI 2U
#define LL_RCC_SYS_CLKSOURCE_STATUS_MSI 3U
#define LL_RCC_SYS_CLKSOURCE_STATUS_IC2_IC6_IC11 4U
#define LL_RCC_MSI_FREQ_16MHZ 16000000U
#define STM32_SYSCLK_SRC_HSE 1
#define STM32_SYSCLK_SRC_HSI 0
#define STM32_SYSCLK_SRC_IC2 0
#define STM32_SYSCLK_SRC_MSI 0
#define STM32_CKPER_ENABLED 0
#define STM32_AHB_PRESCALER 1
#define STM32_APB1_PRESCALER 1
#define STM32_APB2_PRESCALER 1
#define STM32_APB4_PRESCALER 1
#define STM32_APB5_PRESCALER 1
#define STM32_TIMG_PRESCALER 1
#define CONFIG_CLOCK_STM32_N6_PRESERVE_BOOT_CONFIG preserve_boot_config
#define DT_NODELABEL(node) node
#define DT_PROP(node, property) 400000000U
#define ARG_UNUSED(value) ((void)(value))
#define ahb_prescaler(value) 1U
#define apb1_prescaler(value) 1U
#define apb2_prescaler(value) 1U
#define apb4_prescaler(value) 1U
#define apb5_prescaler(value) 1U
#define timg_prescaler(value) 1U
#define LL_RCC_SYS_CLKSOURCE_HSE 1U
#define LL_RCC_SYS_CLKSOURCE_HSI 2U
#define LL_RCC_SYS_CLKSOURCE_IC2_IC6_IC11 3U
#define LL_RCC_SYS_CLKSOURCE_MSI 4U
#define LL_MEM_AXISRAM1 1U
#define LL_MEM_AXISRAM2 2U
#define LL_MEM_AHBSRAM1 4U
#define LL_MEM_AHBSRAM2 8U
#define LL_MEM_BKPSRAM 16U
#define LL_MEM_FLEXRAM 32U
#define LL_MEM_CACHEAXIRAM 64U
#define LL_MEM_VENCRAM 128U

enum fixture_event {
	FIXTURE_MEM_RUN,
	FIXTURE_MEM_LOW_POWER,
	FIXTURE_FIXED_SOURCES,
	FIXTURE_PLL_SETUP,
	FIXTURE_AHB,
	FIXTURE_APB1,
	FIXTURE_APB2,
	FIXTURE_APB4,
	FIXTURE_APB5,
	FIXTURE_TIMG,
	FIXTURE_ICS,
	FIXTURE_SYS_SET,
	FIXTURE_SYS_READ,
	FIXTURE_PLL3_DISABLE,
	FIXTURE_PLL2_DISABLE,
	FIXTURE_CORE_UPDATE,
};

struct device { int unused; };
static uint32_t SystemCoreClock;
static uint32_t sys_source;
static uint32_t hsi_divider;
static uint32_t ic2_source;
static uint32_t ic2_divider;
static uint32_t msi_frequency;
static int preserve_boot_config;
static int events[32];
static unsigned int event_count;
static void record_event(int event) { events[event_count++] = event; }
static void LL_MEM_EnableClock(uint32_t value) { ARG_UNUSED(value); record_event(FIXTURE_MEM_RUN); }
static void LL_MEM_EnableClockLowPower(uint32_t value)
{
	ARG_UNUSED(value);
	record_event(FIXTURE_MEM_LOW_POWER);
}
static void set_up_fixed_clock_sources(void) { record_event(FIXTURE_FIXED_SOURCES); }
static int set_up_plls(void) { record_event(FIXTURE_PLL_SETUP); return 0; }
static int set_up_ics(void) { record_event(FIXTURE_ICS); return 0; }
static void stm32_clock_switch_to_hsi(void) { record_event(FIXTURE_SYS_SET); }
static void LL_RCC_SetAHBPrescaler(uint32_t value) { ARG_UNUSED(value); record_event(FIXTURE_AHB); }
static void LL_RCC_SetAPB1Prescaler(uint32_t value)
{
	ARG_UNUSED(value);
	record_event(FIXTURE_APB1);
}
static void LL_RCC_SetAPB2Prescaler(uint32_t value)
{
	ARG_UNUSED(value);
	record_event(FIXTURE_APB2);
}
static void LL_RCC_SetAPB4Prescaler(uint32_t value)
{
	ARG_UNUSED(value);
	record_event(FIXTURE_APB4);
}
static void LL_RCC_SetAPB5Prescaler(uint32_t value)
{
	ARG_UNUSED(value);
	record_event(FIXTURE_APB5);
}
static void LL_RCC_SetTIMPrescaler(uint32_t value)
{
	ARG_UNUSED(value);
	record_event(FIXTURE_TIMG);
}
static void LL_RCC_SetSysClkSource(uint32_t value)
{
	ARG_UNUSED(value);
	record_event(FIXTURE_SYS_SET);
}
static void LL_RCC_PLL3_Disable(void) { record_event(FIXTURE_PLL3_DISABLE); }
static void LL_RCC_PLL2_Disable(void) { record_event(FIXTURE_PLL2_DISABLE); }
static void LL_MISC_EnableClock(uint32_t value) { ARG_UNUSED(value); }
static void LL_MISC_EnableClockLowPower(uint32_t value) { ARG_UNUSED(value); }
static int LL_MISC_IsEnabledClock(uint32_t value) { ARG_UNUSED(value); return 1; }
#define LL_PER 1U
static void SystemCoreClockUpdate(void)
{
	record_event(FIXTURE_CORE_UPDATE);
	SystemCoreClock = 64000000U;
}

static uint32_t LL_RCC_MSI_GetFrequency(void) { return msi_frequency; }
static uint32_t LL_RCC_GetSysClkSource(void)
{
	record_event(FIXTURE_SYS_READ);
	return sys_source;
}
static uint32_t LL_RCC_HSI_GetDivider(void) { return hsi_divider; }
static uint32_t LL_RCC_IC2_GetSource(void) { return ic2_source; }
static uint32_t LL_RCC_IC2_GetDivider(void) { return ic2_divider; }

"""
	fixture += "\n\n".join(extract_function(source, name) for name in functions[:-1])
	init_source = extract_function(source, "stm32_clock_control_init")
	fixture += "\n\n" + init_source.replace(
		"stm32_clock_control_init", "fixture_init_unconfigured"
	)
	fixture += "\n\n#define STM32_PLL2_ENABLED 1\n#define STM32_PLL3_ENABLED 1\n"
	fixture += init_source.replace("stm32_clock_control_init", "fixture_init_configured")
	fixture += "\n#undef STM32_PLL2_ENABLED\n#undef STM32_PLL3_ENABLED\n"
	fixture += r"""

void fixture_reset(void)
{
	event_count = 0U;
	SystemCoreClock = 0U;
	sys_source = LL_RCC_SYS_CLKSOURCE_STATUS_HSE;
}

int fixture_init(int preserve, int configured_plls)
{
	preserve_boot_config = preserve;
	return configured_plls ? fixture_init_configured(NULL) : fixture_init_unconfigured(NULL);
}

int fixture_event_count(void) { return event_count; }
int fixture_event(unsigned int index) { return events[index]; }
uint32_t fixture_core_rate(void) { return SystemCoreClock; }

uint32_t fixture_pll4_rate(void)
{
	return get_pllout_frequency(PLL4_ID);
}

uint32_t fixture_ic_rate(uint32_t source, uint32_t divider)
{
	return get_icout_frequency(source, divider);
}

uint32_t fixture_sys_rate(uint32_t source, uint32_t hsi_div, uint32_t msi_freq,
			  uint32_t pll_source, uint32_t ic_div)
{
	sys_source = source;
	hsi_divider = hsi_div;
	msi_frequency = msi_freq;
	ic2_source = pll_source;
	ic2_divider = ic_div;
	return get_sysclk_frequency();
}
"""
	pathlib.Path(sys.argv[2]).write_text(fixture)


if __name__ == "__main__":
	main()
