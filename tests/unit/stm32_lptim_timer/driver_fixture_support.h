/*
 * Copyright (c) 2026 Hula Firmware
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef STM32_LPTIM_DRIVER_FIXTURE_SUPPORT_H_
#define STM32_LPTIM_DRIVER_FIXTURE_SUPPORT_H_

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/ztest.h>

#define k_spinlock stm32_lptim_fake_lock
#define k_spinlock_key_t stm32_lptim_fake_lock_key
#define k_spin_lock stm32_lptim_fake_spin_lock
#define k_spin_unlock stm32_lptim_fake_spin_unlock
#define sys_clock_cycle_get_64 stm32_lptim_fixture_cycle_get_64
#define sys_clock_cycle_get_32 stm32_lptim_fixture_cycle_get_32
#define sys_clock_set_timeout stm32_lptim_fixture_set_timeout
#undef sys_clock_hw_cycles_per_sec
#define sys_clock_hw_cycles_per_sec stm32_lptim_fixture_hw_cycles_per_sec
#define sys_clock_announce stm32_lptim_fixture_announce
#define sys_clock_idle_exit stm32_lptim_fixture_idle_exit
#define counter_get_value stm32_lptim_fixture_counter_get_value
#define counter_get_top_value stm32_lptim_fixture_counter_get_top_value
#define counter_ticks_to_us stm32_lptim_fixture_counter_ticks_to_us

#undef CONFIG_STM32_LPTIM_STDBY_TIMER
#undef CONFIG_SYS_CLOCK_TICKS_PER_SEC
#undef CONFIG_STM32_LPTIM_CLOCK
#undef CONFIG_TICKLESS_KERNEL
#undef CONFIG_SYSTEM_CLOCK_SLOPPY_IDLE
#undef USEC_PER_SEC
#define CONFIG_STM32_LPTIM_CLOCK 32768U
#define CONFIG_SYS_CLOCK_TICKS_PER_SEC 4096U
#define CONFIG_TICKLESS_KERNEL 1
#define CONFIG_SYSTEM_CLOCK_SLOPPY_IDLE 0
#define USEC_PER_SEC 1000000U
#define SYS_CLOCK_MAX_WAIT INT_MAX
#define LPTIM_GUARD_VALUE 2U

typedef uint64_t cycle_t;
typedef int stm32_lptim_fake_lock_key;
struct stm32_lptim_fake_lock {
	int unused;
};

static struct stm32_lptim_fake_lock lock;
static int fake_timer;
#define LPTIM (&fake_timer)
static const struct device *stdby_timer;
static bool timeout_stdby;
static uint64_t lptim_cnt_pre_stdby;
static uint32_t stdby_timer_pre_stdby;
static uint64_t accumulated_lptim_cnt;
static uint32_t lptim_clock_freq = 32768U;
static bool fake_arrm_active;
static bool fake_arrm_enabled = true;
static uint32_t fake_autoreload = UINT16_MAX;
static uint32_t fake_counter;
static uint32_t fake_hw_cycles_per_sec = 32768U;
static uint32_t fake_counter_post;
static uint32_t fake_counter_top = UINT16_MAX;
static uint64_t fake_us_per_tick = 1000000U;
static uint32_t fake_announced_ticks;
static uint32_t fake_lock_depth;
static uint32_t fake_lock_count;
static uint32_t fake_autoreload_writes;
static bool fake_arrok_active;
static bool fake_arrok_enabled;

typedef void *clock_control_subsys_t;
static void *clk_ctrl;
static uint32_t lptim_clk[2];
static uint32_t lptim_time_base = 0x10000U;
static uint32_t autoreload_next;
static bool autoreload_ready = true;

static inline int clock_control_on(void *dev, clock_control_subsys_t subsys)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(subsys);
	return 0;
}

static inline int clock_control_off(void *dev, clock_control_subsys_t subsys)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(subsys);
	return 0;
}

static inline void LL_LPTIM_ClearFlag_ARROK(void *timer)
{
	ARG_UNUSED(timer);
}

static inline void LL_LPTIM_SetAutoReload(void *timer, uint32_t value)
{
	ARG_UNUSED(timer);
	fake_autoreload = value;
	fake_autoreload_writes++;
}

static inline bool LL_LPTIM_IsActiveFlag_ARRM(void *timer)
{
	ARG_UNUSED(timer);
	return fake_arrm_active;
}

static inline bool LL_LPTIM_IsEnabledIT_ARRM(void *timer)
{
	ARG_UNUSED(timer);
	return fake_arrm_enabled;
}

static inline uint32_t LL_LPTIM_GetAutoReload(void *timer)
{
	ARG_UNUSED(timer);
	return fake_autoreload;
}

static inline uint32_t LL_LPTIM_GetCounter(void *timer)
{
	ARG_UNUSED(timer);
	return fake_counter;
}

static inline uint32_t LL_LPTIM_IsActiveFlag_ARROK(void *timer)
{
	ARG_UNUSED(timer);
	return fake_arrok_active;
}

static inline uint32_t LL_LPTIM_IsEnabledIT_ARROK(void *timer)
{
	ARG_UNUSED(timer);
	return fake_arrok_enabled;
}

static inline void LL_LPTIM_ClearFlag_ARRM(void *timer)
{
	ARG_UNUSED(timer);
	fake_arrm_active = false;
}

static inline stm32_lptim_fake_lock_key stm32_lptim_fake_spin_lock(
	struct stm32_lptim_fake_lock *spinlock)
{
	ARG_UNUSED(spinlock);
	fake_lock_count++;
	fake_lock_depth++;
	return (stm32_lptim_fake_lock_key)fake_lock_depth;
}

static inline void stm32_lptim_fake_spin_unlock(struct stm32_lptim_fake_lock *spinlock,
							stm32_lptim_fake_lock_key key)
{
	ARG_UNUSED(spinlock);
	ARG_UNUSED(key);
	fake_lock_depth--;
}

static inline uint32_t stm32_lptim_fixture_hw_cycles_per_sec(void)
{
	return fake_hw_cycles_per_sec;
}

static inline void LL_LPTIM_ResetCounter(void *timer)
{
	ARG_UNUSED(timer);
	fake_counter = 0U;
}

static inline int stm32_lptim_fixture_counter_get_value(const struct device *dev,
							uint32_t *value)
{
	ARG_UNUSED(dev);
	*value = fake_counter_post;
	return 0;
}

static inline uint32_t stm32_lptim_fixture_counter_get_top_value(const struct device *dev)
{
	ARG_UNUSED(dev);
	return fake_counter_top;
}

static inline uint64_t stm32_lptim_fixture_counter_ticks_to_us(const struct device *dev,
								      uint32_t ticks)
{
	ARG_UNUSED(dev);
	return (uint64_t)ticks * fake_us_per_tick;
}

static inline void stm32_lptim_fixture_announce(uint32_t ticks)
{
	fake_announced_ticks = ticks;
}

uint64_t sys_clock_cycle_get_64(void);
uint32_t sys_clock_cycle_get_32(void);
static void lptim_irq_handler(const struct device *unused);

#endif /* STM32_LPTIM_DRIVER_FIXTURE_SUPPORT_H_ */
