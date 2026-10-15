/*
 * Copyright (c) 2026 Hula Firmware
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_STM32_XSPI_TIMING_H_
#define ZEPHYR_INCLUDE_DRIVERS_STM32_XSPI_TIMING_H_

#ifdef CONFIG_N6_BOOT_TIMING
#include <stdint.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

void n6_boot_timing_record(const char *phase, int area, uint32_t bytes, int64_t start_ms,
			   int result);

#ifdef __cplusplus
}
#endif

#define STM32_XSPI_INIT_TIME_BEGIN(name) const int64_t name = k_uptime_get()
#define STM32_XSPI_INIT_TIME_END(phase, start, result) \
	n6_boot_timing_record(phase, -1, 0U, start, result)
#else
#define STM32_XSPI_INIT_TIME_BEGIN(name)
#define STM32_XSPI_INIT_TIME_END(phase, start, result) do { } while (0)
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_STM32_XSPI_TIMING_H_ */
