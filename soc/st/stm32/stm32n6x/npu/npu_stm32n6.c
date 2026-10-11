/*
 * Copyright (c) 2025 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT st_stm32_npu

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/init.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/util.h>
#include <soc.h>

#include <zephyr/drivers/clock_control/stm32_clock_control.h>

BUILD_ASSERT(!(IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME) &&
	(IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME_DEFAULT_ENABLE) ||
	 DT_INST_PROP_OR(0, zephyr_pm_device_runtime_auto, false))),
	"STM32N6 NPU automatic runtime PM is not supported");

/* Read-only driver configuration */
struct npu_stm32_cfg {
	/* Clock configuration. */
	struct stm32_pclken pclken_npu;
	struct stm32_pclken pclken_cacheaxi;
	/* Reset configuration */
	const struct reset_dt_spec reset_npu;
	const struct reset_dt_spec reset_cacheaxi;
};

static int npu_stm32_clock_on(const struct device *dev)
{
	const struct device *const clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
	const struct npu_stm32_cfg *cfg = dev->config;
	int ret;

	ret = clock_control_on(clk, (clock_control_subsys_t)&cfg->pclken_npu);
	if (ret != 0) {
		return ret;
	}

	ret = clock_control_on(clk, (clock_control_subsys_t)&cfg->pclken_cacheaxi);
	if (ret != 0) {
		(void)clock_control_off(clk, (clock_control_subsys_t)&cfg->pclken_npu);
		return ret;
	}

	return 0;
}

static int npu_stm32_clock_off(const struct device *dev)
{
	const struct device *const clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
	const struct npu_stm32_cfg *cfg = dev->config;
	int ret;

	ret = clock_control_off(clk, (clock_control_subsys_t)&cfg->pclken_npu);
	if (ret != 0) {
		return ret;
	}

	ret = clock_control_off(clk, (clock_control_subsys_t)&cfg->pclken_cacheaxi);
	if (ret != 0) {
		(void)clock_control_on(clk, (clock_control_subsys_t)&cfg->pclken_npu);
		return ret;
	}

	return 0;
}

static int npu_stm32_pm_action(const struct device *dev, enum pm_device_action action)
{
	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		return npu_stm32_clock_on(dev);
	case PM_DEVICE_ACTION_SUSPEND:
		return npu_stm32_clock_off(dev);
	default:
		return -ENOTSUP;
	}
}

static int npu_stm32_init(const struct device *dev)
{
	const struct device *const clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
	const struct npu_stm32_cfg *cfg = dev->config;
	int ret;

	if (!device_is_ready(clk)) {
		return -ENODEV;
	}

	ret = npu_stm32_clock_on(dev);
	if (ret != 0) {
		return ret;
	}

	if (!device_is_ready(cfg->reset_npu.dev)) {
		(void)npu_stm32_clock_off(dev);
		return -ENODEV;
	}

	/* Reset timer to default state using RCC */
	(void)reset_line_toggle_dt(&cfg->reset_npu);
	(void)reset_line_toggle_dt(&cfg->reset_cacheaxi);

	return pm_device_driver_init(dev, npu_stm32_pm_action);
}

PM_DEVICE_DT_INST_DEFINE(0, npu_stm32_pm_action);

static const struct npu_stm32_cfg npu_stm32_cfg = {
	.pclken_npu = STM32_DT_INST_CLOCK_INFO(0),
	.reset_npu = RESET_DT_SPEC_INST_GET(0),
	/*
	 * Even if npu_cache node is disabled, its clocks must be enabled for NPU operation.
	 * This is why we need to get clock and reset line from the npu_cache node.
	 */
	.pclken_cacheaxi = STM32_CLOCK_INFO(0, DT_NODELABEL(npu_cache)),
	.reset_cacheaxi = RESET_DT_SPEC_GET(DT_NODELABEL(npu_cache)),
};

DEVICE_DT_INST_DEFINE(0, npu_stm32_init, PM_DEVICE_DT_INST_GET(0), NULL,
		      &npu_stm32_cfg, POST_KERNEL, CONFIG_STM32N6_NPU_INIT_PRIORITY, NULL);
