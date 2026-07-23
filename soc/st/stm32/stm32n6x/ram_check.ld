/*
 * Copyright (c) 2025 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <zephyr/linker/linker-defs.h>

#if DT_HAS_CHOSEN(mcuboot_ram_load_dev)
#define MCUBOOT_RAMLOAD_END_ADDR \
	(DT_REG_ADDR(DT_CHOSEN(mcuboot_ram_load_dev)) + DT_REG_SIZE(DT_CHOSEN(mcuboot_ram_load_dev)))
#else
#define MCUBOOT_RAMLOAD_END_ADDR DT_REG_ADDR(DT_NODELABEL(axisram2))
#endif

ASSERT((__kernel_ram_start < (MCUBOOT_RAMLOAD_END_ADDR - CONFIG_ROM_START_OFFSET)),
       "
       Image is too large to fit RAMLOAD configuration. Consider using XIP.
       ")
