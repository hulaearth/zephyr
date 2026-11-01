/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Hula firmware maintainers
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_PM_STM32N6_RETAINED_STOP_H_
#define ZEPHYR_INCLUDE_PM_STM32N6_RETAINED_STOP_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Last-stage code explaining why a retained Stop attempt did not enter Stop. */
enum stm32n6_retained_stop_rejection {
	STM32N6_RETAINED_STOP_REJECTION_NONE = 0,
	STM32N6_RETAINED_STOP_REJECTION_INELIGIBLE = 1,
	STM32N6_RETAINED_STOP_REJECTION_PROFILE = 2,
	STM32N6_RETAINED_STOP_REJECTION_WAKE_GATES = 3,
	STM32N6_RETAINED_STOP_REJECTION_POWER_CONFIG = 4,
	STM32N6_RETAINED_STOP_REJECTION_STOP_FLAG = 5,
	STM32N6_RETAINED_STOP_REJECTION_XSPI_BUSY = 6,
	STM32N6_RETAINED_STOP_REJECTION_DMA_ACTIVE = 7,
};

/** Saturating counters for retained Stop attempts and completed wakes. */
struct stm32n6_retained_stop_stats {
	uint32_t attempts;
	uint32_t entries;
	uint32_t wakes;
	uint32_t rejections;
	uint32_t last_rejection;
};

/**
 * @brief Enable or disable retained Stop entry. Initially disabled.
 *
 * @param enabled Allow entry when the retained clock profile is valid.
 * @retval 0 The runtime request was stored.
 */
int stm32n6_retained_stop_set_enabled(bool enabled);

/**
 * @brief Read a counter snapshot in thread context.
 *
 * Rejections count attempts that did not enter Stop. last_rejection reports
 * the last stage that prevented entry during the most recently enabled test;
 * it is reset when Stop is enabled and retained when disabled. A non-returning
 * restoration fault leaves entries ahead of wakes.
 *
 * @param stats Non-NULL destination for the counter snapshot.
 */
void stm32n6_retained_stop_get_stats(struct stm32n6_retained_stop_stats *stats);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_PM_STM32N6_RETAINED_STOP_H_ */
