/*
 * Copyright (c) 2026 Hula Firmware
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

uint32_t fixture_pll4_rate(void);
uint32_t fixture_ic_rate(uint32_t source, uint32_t divider);
uint32_t fixture_ic1_subsys_rate(uint32_t source, uint32_t divider);
uint32_t fixture_sys_rate(uint32_t source, uint32_t hsi_div, uint32_t msi_freq,
			  uint32_t pll_source, uint32_t ic_div);
void fixture_reset(void);
int fixture_init(int preserve, int configured_plls);
int fixture_event_count(void);
int fixture_event(unsigned int index);
uint32_t fixture_core_rate(void);

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

ZTEST_SUITE(stm32n6_clock, NULL, NULL, NULL, NULL, NULL);

ZTEST(stm32n6_clock, test_pll_rate_keeps_fractional_intermediate)
{
	zassert_equal(fixture_pll4_rate(), 245760000U);
	zassert_equal(fixture_ic_rate(4U, 4U), 61440000U);
}

ZTEST(stm32n6_clock, test_sys_rate_reads_live_source_and_dividers)
{
	const uint32_t hse = 1U;
	const uint32_t hsi = 2U;
	const uint32_t msi = 3U;
	const uint32_t ic2 = 4U;

	zassert_equal(fixture_sys_rate(hse, 0U, 0U, 0U, 0U), 32768000U);
	zassert_equal(fixture_sys_rate(hsi, 0U, 0U, 0U, 0U), 64000000U);
	zassert_equal(fixture_sys_rate(hsi, 1U << 7, 0U, 0U, 0U), 32000000U);
	zassert_equal(fixture_sys_rate(hsi, 2U << 7, 0U, 0U, 0U), 16000000U);
	zassert_equal(fixture_sys_rate(hsi, 3U << 7, 0U, 0U, 0U), 8000000U);
	zassert_equal(fixture_sys_rate(msi, 0U, 4000000U, 0U, 0U), 4000000U);
	zassert_equal(fixture_sys_rate(ic2, 0U, 0U, 4U, 5U), 49152000U);
	zassert_equal(fixture_sys_rate(99U, 0U, 0U, 0U, 0U), 0U);
}

ZTEST(stm32n6_clock, test_ic_source_with_dynamic_divider)
{
	zassert_equal(fixture_ic_rate(4U, 5U), 49152000U);
}

ZTEST(stm32n6_clock, test_ic1_subsys_rate_uses_live_divider)
{
	/* DT config is 2, while the accepted HAL getter supplies the live value 5. */
	zassert_equal(fixture_ic1_subsys_rate(4U, 5U), 49152000U);
}

ZTEST(stm32n6_clock, test_normal_init_releases_only_unconfigured_plls_after_handoff)
{
	static const int expected[] = {
		FIXTURE_MEM_RUN, FIXTURE_MEM_LOW_POWER, FIXTURE_FIXED_SOURCES,
		FIXTURE_PLL_SETUP, FIXTURE_AHB, FIXTURE_APB1, FIXTURE_APB2,
		FIXTURE_APB4, FIXTURE_APB5, FIXTURE_TIMG, FIXTURE_ICS,
		FIXTURE_SYS_SET, FIXTURE_SYS_READ, FIXTURE_PLL3_DISABLE,
		FIXTURE_PLL2_DISABLE,
	};

	fixture_reset();
	zassert_ok(fixture_init(0, 0));
	zassert_equal(fixture_core_rate(), 400000000U);
	zassert_equal(fixture_event_count(), ARRAY_SIZE(expected));
	for (unsigned int i = 0U; i < ARRAY_SIZE(expected); ++i) {
		zassert_equal(fixture_event(i), expected[i]);
	}
}

ZTEST(stm32n6_clock, test_normal_init_keeps_configured_plls_enabled)
{
	fixture_reset();
	zassert_ok(fixture_init(0, 1));
	for (int i = 0; i < fixture_event_count(); ++i) {
		zassert_not_equal(fixture_event(i), FIXTURE_PLL3_DISABLE);
		zassert_not_equal(fixture_event(i), FIXTURE_PLL2_DISABLE);
	}
}

ZTEST(stm32n6_clock, test_preserve_init_only_opens_sram_and_updates_core_rate)
{
	static const int expected[] = {
		FIXTURE_MEM_RUN, FIXTURE_MEM_LOW_POWER, FIXTURE_CORE_UPDATE,
	};

	fixture_reset();
	zassert_ok(fixture_init(1, 0));
	zassert_equal(fixture_core_rate(), 64000000U);
	zassert_equal(fixture_event_count(), ARRAY_SIZE(expected));
	for (unsigned int i = 0U; i < ARRAY_SIZE(expected); ++i) {
		zassert_equal(fixture_event(i), expected[i]);
	}
}
