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

/** Saturating counters for retained Stop attempts and completed wakes. */
struct stm32n6_retained_stop_stats {
	uint32_t attempts;
	uint32_t entries;
	uint32_t wakes;
	uint32_t rejections;
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
 * Profile mismatches increment rejections. A non-returning restoration fault
 * leaves entries ahead of wakes.
 *
 * @param stats Non-NULL destination for the counter snapshot.
 */
void stm32n6_retained_stop_get_stats(struct stm32n6_retained_stop_stats *stats);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_PM_STM32N6_RETAINED_STOP_H_ */
