/*
 * Copyright 2025 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/util.h>
#include <soc.h>

#include <zephyr/drivers/clock_control/stm32_clock_control.h>

#define DT_DRV_COMPAT st_stm32n6_ramcfg

/* Read-only driver configuration */
struct axisram_stm32_cfg {
	/* RAMCFG instance. */
	RAMCFG_TypeDef *base;
	/* SRAM Clock configuration. */
	struct stm32_pclken pclken_axisram;
	/* RAMCFG Clock configuration. */
	struct stm32_pclken pclken_ramcfg;
};

static int axisram_stm32_resume(const struct device *dev)
{
	const struct axisram_stm32_cfg *cfg = dev->config;
	RAMCFG_HandleTypeDef ramcfg = {.Instance = cfg->base};
	const struct device *const clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
	int ret;

	/* RAMCFG is shared by all banks; keep its gate enabled across transitions. */
	ret = clock_control_on(clk, (clock_control_subsys_t)&cfg->pclken_ramcfg);
	if (ret != 0) {
		return ret;
	}

	ret = clock_control_on(clk, (clock_control_subsys_t)&cfg->pclken_axisram);
	if (ret != 0) {
		return ret;
	}

	HAL_RAMCFG_EnableAXISRAM(&ramcfg);
	return 0;
}

static int axisram_stm32_suspend(const struct device *dev)
{
	const struct axisram_stm32_cfg *cfg = dev->config;
	RAMCFG_HandleTypeDef ramcfg = {.Instance = cfg->base};
	const struct device *const clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
	int ret;

	/* Powering off a bank destroys its contents, including on a later failure. */
	HAL_RAMCFG_DisableAXISRAM(&ramcfg);
	ret = clock_control_off(clk, (clock_control_subsys_t)&cfg->pclken_axisram);
	if (ret != 0) {
		HAL_RAMCFG_EnableAXISRAM(&ramcfg);
	}
	return ret;
}

static int axisram_stm32_pm_action(const struct device *dev, enum pm_device_action action)
{
	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		return axisram_stm32_resume(dev);
	case PM_DEVICE_ACTION_SUSPEND:
		return axisram_stm32_suspend(dev);
	default:
		return -ENOTSUP;
	}
}

static int axisram_stm32_init(const struct device *dev)
{
	const struct device *const clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
	int ret;

	if (!device_is_ready(clk)) {
		return -ENODEV;
	}

	ret = axisram_stm32_resume(dev);
	if (ret != 0) {
		return ret;
	}

	return pm_device_driver_init(dev, axisram_stm32_pm_action);
}

#define STM32N6_AXISRAM_INIT(idx)							\
	BUILD_ASSERT(!(IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME) &&			\
		(IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME_DEFAULT_ENABLE) ||		\
		 DT_INST_PROP_OR(idx, zephyr_pm_device_runtime_auto, false))),	\
		"STM32N6 AXISRAM automatic runtime PM is not supported");		\
	static const struct axisram_stm32_cfg axisram_stm32_cfg_##idx = {		\
		.base = (RAMCFG_TypeDef *)DT_INST_REG_ADDR(idx),			\
		.pclken_axisram = STM32_DT_INST_CLOCK_INFO_BY_NAME(idx, axisram),	\
		.pclken_ramcfg = STM32_DT_INST_CLOCK_INFO_BY_NAME(idx, ramcfg),		\
	};										\
											\
	PM_DEVICE_DT_INST_DEFINE(idx, axisram_stm32_pm_action);			\
										\
	DEVICE_DT_INST_DEFINE(idx, &axisram_stm32_init, PM_DEVICE_DT_INST_GET(idx),	\
			      NULL, &axisram_stm32_cfg_##idx,				\
			      PRE_KERNEL_2, 0, NULL);

/**
 * On other series which have no RAMCFG, whether RAMs are enabled
 * or not can be controlled by changing their "status" in Device Tree.
 * To match this behavior on N6, we check manually during instantiation
 * of RAMCFG nodes whether they have an enabled child (= RAM node) and
 * perform our own instantiation only if so thanks to COND_CODE.
 */
#define STM32N6_AXISRAM_MAYBE_INIT(idx)							\
	COND_CODE_0(DT_INST_CHILD_NUM_STATUS_OKAY(idx), (), (STM32N6_AXISRAM_INIT(idx)))

DT_INST_FOREACH_STATUS_OKAY(STM32N6_AXISRAM_MAYBE_INIT)
