/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ams_ens210

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "ens210.h"

struct ens210_emul_data {
	bool active;
	bool reset_seen;
	int reset_error;
	int enable_error;
	int64_t reset_time;
	int64_t enable_delay;
};

static int ens210_transfer(const struct emul *emulator, struct i2c_msg *msgs,
			  int num_msgs, int addr)
{
	struct ens210_emul_data *data = emulator->data;
	uint8_t reg = msgs[0].buf[0];

	ARG_UNUSED(addr);
	if (num_msgs == 1 && msgs[0].len == 2) {
		uint8_t value = msgs[0].buf[1];

		if (reg == ENS210_REG_SYS_CTRL) {
			if (value & BIT(7)) {
				data->reset_seen = true;
				data->reset_time = k_uptime_get();
				return data->reset_error;
			}
			if (data->reset_seen) {
				data->enable_delay = k_uptime_get() - data->reset_time;
				/* Model a device that NACKs until reset has completed. */
				if (data->enable_delay < 2) {
					return -EBUSY;
				}
			}
			if (data->enable_error != 0) {
				return data->enable_error;
			}
			data->active = true;
		}
		return 0;
	}

	if (num_msgs != 2 || msgs[0].len != 1 || !(msgs[1].flags & I2C_MSG_READ)) {
		return -EIO;
	}

	switch (reg) {
	case ENS210_REG_SYS_STAT:
		msgs[1].buf[0] = data->active;
		return 0;
	case ENS210_REG_PART_ID:
		sys_put_le16(ENS210_PART_ID, msgs[1].buf);
		return 0;
	default:
		return -EIO;
	}
}

static int ens210_emul_init(const struct emul *emulator, const struct device *parent)
{
	ARG_UNUSED(emulator);
	ARG_UNUSED(parent);
	return 0;
}

static const struct i2c_emul_api ens210_emul_api = {
	.transfer = ens210_transfer,
};

#define ENS210_EMUL(inst)                                                            \
	static struct ens210_emul_data ens210_emul_data_##inst;                       \
	EMUL_DT_INST_DEFINE(inst, ens210_emul_init, &ens210_emul_data_##inst, NULL,     \
			    &ens210_emul_api, NULL);

DT_INST_FOREACH_STATUS_OKAY(ENS210_EMUL)

ZTEST(ens210, test_reset_delay)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(ens210));
	struct ens210_emul_data *data = EMUL_DT_GET(DT_NODELABEL(ens210))->data;

	zassert_true(device_is_ready(dev));
	zassert_true(data->reset_seen);
	zassert_true(data->enable_delay >= 2);
}

ZTEST(ens210, test_reset_error)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(reset_error));
	struct ens210_emul_data *data = EMUL_DT_GET(DT_NODELABEL(reset_error))->data;

	data->reset_error = -ENXIO;
	zassert_equal(device_init(dev), -ENXIO);
	zassert_false(device_is_ready(dev));
	zassert_false(data->active);
}

ZTEST(ens210, test_enable_error)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(enable_error));
	struct ens210_emul_data *data = EMUL_DT_GET(DT_NODELABEL(enable_error))->data;

	data->enable_error = -ENXIO;
	zassert_equal(device_init(dev), -ENXIO);
	zassert_false(device_is_ready(dev));
	zassert_true(data->reset_seen);
	zassert_false(data->active);
}

static void *ens210_setup(void)
{
	zassert_ok(device_init(DEVICE_DT_GET(DT_NODELABEL(ens210))));
	return NULL;
}

ZTEST_SUITE(ens210, NULL, ens210_setup, NULL, NULL, NULL);
