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

/**
 * @brief Enable or disable retained Stop entry. Initially disabled.
 *
 * @param enabled Allow entry when the retained clock profile is valid.
 * @retval 0 The runtime request was stored.
 */
int stm32n6_retained_stop_set_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_PM_STM32N6_RETAINED_STOP_H_ */
