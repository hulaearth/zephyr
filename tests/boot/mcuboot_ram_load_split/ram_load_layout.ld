/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>

#if defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_SINGLE_APP_RAM_LOAD) && \
	DT_HAS_CHOSEN(mcuboot_ram_load_dev)
#define IMAGE_RAM_NODE DT_CHOSEN(mcuboot_ram_load_dev)
#elif !defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_SINGLE_APP_RAM_LOAD) && \
	DT_HAS_CHOSEN(mcuboot_image_ram)
#define IMAGE_RAM_NODE DT_CHOSEN(mcuboot_image_ram)
#else
#define IMAGE_RAM_NODE DT_CHOSEN(zephyr_sram)
#endif

ASSERT(__rom_region_start == DT_REG_ADDR(IMAGE_RAM_NODE),
	"image is not linked at the MCUboot RAM-load address")
ASSERT(__data_region_load_start >= DT_REG_ADDR(IMAGE_RAM_NODE),
	"initialized data is not loaded from the image region")
ASSERT(__data_region_load_start <
	DT_REG_ADDR(IMAGE_RAM_NODE) + DT_REG_SIZE(IMAGE_RAM_NODE),
	"initialized data load address exceeds the image region")
ASSERT(__data_region_start >= DT_REG_ADDR(DT_CHOSEN(zephyr_sram)),
	"initialized data is not placed in runtime SRAM")
#if DT_REG_ADDR(IMAGE_RAM_NODE) != DT_REG_ADDR(DT_CHOSEN(zephyr_sram))
ASSERT(__data_region_start != __data_region_load_start,
	"initialized data does not have split load and runtime addresses")
#else
ASSERT(__data_region_start == __data_region_load_start,
	"initialized data should already be in runtime SRAM")
#endif
ASSERT(__bss_start >= DT_REG_ADDR(DT_CHOSEN(zephyr_sram)),
	"BSS is not placed in runtime SRAM")
