/*
 * Copyright (c) 2026 Hula Firmware
 *
 * SPDX-License-Identifier: Apache-2.0
 */

ZTEST(stm32_lptim_timer, test_pending_reload_uses_full_period)
{
	fake_arrm_active = true;
	fake_arrm_enabled = true;
	fake_autoreload = UINT16_MAX;
	fake_counter = 7U;

	zassert_equal(65543U, sys_clock_lp_time_get());
}

ZTEST(stm32_lptim_timer, test_cycle_counter_is_continuous_across_full_arr_irq)
{
	uint64_t before_irq;
	uint64_t after_irq;

	accumulated_lptim_cnt = 0U;
	fake_arrm_active = true;
	fake_arrm_enabled = true;
	fake_autoreload = UINT16_MAX;
	fake_counter = 0U;
	lptim_clock_freq = 32768U;
	fake_hw_cycles_per_sec = lptim_clock_freq;
	fake_announced_ticks = 0U;

	before_irq = sys_clock_cycle_get_64();
	zassert_equal(0x10000U, before_irq);
	zassert_equal((uint32_t)before_irq, sys_clock_cycle_get_32());

	lptim_irq_handler(NULL);
	after_irq = sys_clock_cycle_get_64();
	zassert_equal(before_irq, after_irq);
	zassert_equal(0x10000U, accumulated_lptim_cnt);
	zassert_equal(8192U, fake_announced_ticks);
	zassert_equal(0U, fake_lock_depth);

	fake_counter = 3U;
	zassert_equal(after_irq + 3U, sys_clock_cycle_get_64());
}

ZTEST(stm32_lptim_timer, test_max_timeout_clamps_to_full_period_arr)
{
	fake_arrm_active = false;
	fake_counter = 0U;
	fake_autoreload = UINT16_MAX - 1U;
	fake_autoreload_writes = 0U;
	autoreload_ready = true;
	lptim_clock_freq = 32768U;
	lptim_time_base = 0x10000U;

	sys_clock_set_timeout(SYS_CLOCK_MAX_WAIT, false);

	zassert_equal(UINT16_MAX, fake_autoreload);
	zassert_equal(1U, fake_autoreload_writes);
	zassert_equal(0U, fake_lock_depth);

	fake_autoreload = UINT16_MAX - 1U;
	fake_autoreload_writes = 0U;
	autoreload_ready = true;
	lptim_clock_freq = 32000U;
	lptim_time_base = 0xFA00U;

	sys_clock_set_timeout(SYS_CLOCK_MAX_WAIT, false);

	zassert_equal(0xF9FFU, fake_autoreload);
	zassert_equal(1U, fake_autoreload_writes);
	zassert_equal(0U, fake_lock_depth);
}

ZTEST(stm32_lptim_timer, test_cycle_get_64_scales_before_widened_product_overflows)
{
	uint64_t expected = 2147483648000000ULL;

	accumulated_lptim_cnt = 1ULL << 40;
	fake_counter = 0U;
	fake_arrm_active = false;
	fake_hw_cycles_per_sec = 64000000U;
	lptim_clock_freq = 32768U;

	zassert_equal(expected, sys_clock_cycle_get_64());
	zassert_equal((uint32_t)expected, sys_clock_cycle_get_32());
	zassert_equal(0U, fake_lock_depth);
}

ZTEST(stm32_lptim_timer, test_standby_elapsed_cycles_update_under_lock)
{
	uint64_t cycle_count;

	/* Model a 32,768 Hz source divided by the supported prescaler of two. */
	lptim_clock_freq = CONFIG_STM32_LPTIM_CLOCK / 2U;
	fake_hw_cycles_per_sec = CONFIG_STM32_LPTIM_CLOCK;
	accumulated_lptim_cnt = 1ULL << 32;
	timeout_stdby = true;
	lptim_cnt_pre_stdby = 200U;
	stdby_timer_pre_stdby = 100U;
	fake_counter_post = 110U;
	fake_us_per_tick = 100000U;
	fake_arrm_active = false;
	fake_counter = 0U;
	fake_lock_count = 0U;
	fake_lock_depth = 0U;

	sys_clock_idle_exit();

	zassert_equal((1ULL << 32) + 16584U, accumulated_lptim_cnt);
	zassert_equal(1U, fake_lock_count);
	zassert_equal(0U, fake_lock_depth);
	zassert_equal(4146U, fake_announced_ticks);
	zassert_false(timeout_stdby);

	cycle_count = sys_clock_cycle_get_64();
	zassert_equal((1ULL << 33) + 33168U, cycle_count);
	zassert_equal(2U, fake_lock_count);
	zassert_equal(0U, fake_lock_depth);
}

ZTEST_SUITE(stm32_lptim_timer, NULL, NULL, NULL, NULL, NULL);
