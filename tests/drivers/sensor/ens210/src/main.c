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
	uint8_t start_mask;
	int start_writes;
	int status_reads;
	int value_reads;
	int busy_reads;
	bool stuck_busy;
	int start_error;
	int status_error;
	bool delayed_start;
	bool previous_valid;
	int64_t start_time;
	uint32_t start_delay_ms;
	uint32_t conversion_ms;
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
		} else if (reg == ENS210_REG_SENS_START) {
			data->start_mask = value;
			data->start_writes++;
			data->start_time = k_uptime_get();
			return data->start_error;
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
	case ENS210_REG_SENS_STAT:
		data->status_reads++;
		if (data->status_error != 0) {
			return data->status_error;
		}
		/* Continuous channels stay busy while single-shot channels finish. */
		msgs[1].buf[0] = ENS210_T_RUN | (ENS210_H_RUN << 1);
		if (data->delayed_start) {
			int64_t elapsed = k_uptime_get() - data->start_time;

			/* Idle while queued, busy in its step, then idle with new data. */
			if (elapsed >= data->start_delay_ms &&
			    elapsed < data->start_delay_ms + data->conversion_ms) {
				msgs[1].buf[0] |= data->start_mask;
			}
		} else if (data->stuck_busy || data->status_reads <= data->busy_reads) {
			msgs[1].buf[0] |= data->start_mask;
		}
		return 0;
	case ENS210_REG_T_VAL:
		data->value_reads++;
		if (data->delayed_start &&
		    k_uptime_get() - data->start_time <
			    data->start_delay_ms + data->conversion_ms) {
			/* A queued request may retain stale validity; start clears it. */
			uint32_t valid = data->previous_valid &&
				(k_uptime_get() - data->start_time < data->start_delay_ms) ?
				BIT(16) : 0;

			sys_put_le24(valid | 18000, &msgs[1].buf[0]);
			sys_put_le24(valid | 24000, &msgs[1].buf[3]);
			return 0;
		}
		/* Valid temperature and humidity; CRC checking is disabled here. */
		sys_put_le24(BIT(16) | 19000, &msgs[1].buf[0]);
		sys_put_le24(BIT(16) | 25000, &msgs[1].buf[3]);
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

static uint8_t single_shot_mask(enum sensor_channel channel)
{
	uint8_t mask = 0;

	if (IS_ENABLED(CONFIG_ENS210_TEMPERATURE_SINGLE) &&
	    (channel == SENSOR_CHAN_ALL || channel == SENSOR_CHAN_AMBIENT_TEMP)) {
		mask |= BIT(0);
	}
	if (IS_ENABLED(CONFIG_ENS210_HUMIDITY_SINGLE) &&
	    (channel == SENSOR_CHAN_ALL || channel == SENSOR_CHAN_HUMIDITY)) {
		mask |= BIT(1);
	}
	return mask;
}

static void ens210_before(void *fixture)
{
	struct ens210_emul_data *data = EMUL_DT_GET(DT_NODELABEL(ens210))->data;

	ARG_UNUSED(fixture);
	data->start_mask = 0;
	data->start_writes = 0;
	data->status_reads = 0;
	data->value_reads = 0;
	data->busy_reads = 2;
	data->stuck_busy = false;
	data->start_error = 0;
	data->status_error = 0;
	data->delayed_start = false;
	data->previous_valid = false;
}

ZTEST(ens210, test_requested_channels)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(ens210));
	struct ens210_emul_data *data = EMUL_DT_GET(DT_NODELABEL(ens210))->data;
	const enum sensor_channel channels[] = {
		SENSOR_CHAN_AMBIENT_TEMP, SENSOR_CHAN_HUMIDITY, SENSOR_CHAN_ALL,
	};

	for (int i = 0; i < ARRAY_SIZE(channels); i++) {
		uint8_t mask = single_shot_mask(channels[i]);

		ens210_before(NULL);
		zassert_ok(sensor_sample_fetch_chan(dev, channels[i]));
		zassert_equal(data->start_mask, mask);
		zassert_equal(data->start_writes, mask != 0);
		zassert_equal(data->status_reads, mask != 0 ? 3 : 0);
		zassert_equal(data->value_reads, 1);
	}
}

ZTEST(ens210, test_start_error)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(ens210));
	struct ens210_emul_data *data = EMUL_DT_GET(DT_NODELABEL(ens210))->data;

	if (single_shot_mask(SENSOR_CHAN_ALL) == 0) {
		ztest_test_skip();
	}
	data->start_error = -ENXIO;
	zassert_equal(sensor_sample_fetch(dev), -ENXIO);
	zassert_equal(data->status_reads, 0);
	zassert_equal(data->value_reads, 0);
}

ZTEST(ens210, test_status_error)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(ens210));
	struct ens210_emul_data *data = EMUL_DT_GET(DT_NODELABEL(ens210))->data;

	if (single_shot_mask(SENSOR_CHAN_ALL) == 0) {
		ztest_test_skip();
	}
	data->status_error = -ENXIO;
	zassert_equal(sensor_sample_fetch(dev), -ENXIO);
	zassert_equal(data->status_reads, 1);
	zassert_equal(data->value_reads, 0);
}

ZTEST(ens210, test_conversion_timeout)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(ens210));
	struct ens210_emul_data *data = EMUL_DT_GET(DT_NODELABEL(ens210))->data;
	int64_t start;

	if (single_shot_mask(SENSOR_CHAN_ALL) == 0) {
		ztest_test_skip();
	}
	data->stuck_busy = true;
	start = k_uptime_get();
	zassert_equal(sensor_sample_fetch(dev), -ETIMEDOUT);
	/* Hardware maxima, with room for native_sim's 10 ms ticks and bus margin. */
	int conversion_ms = (ENS210_T_RUN || ENS210_H_RUN) ? 476 : 130;

	zassert_between_inclusive(k_uptime_get() - start, conversion_ms, conversion_ms + 54);
	zassert_equal(data->value_reads, 0);
}

/* Exercise both mixed configurations with a near-maximum preceding step. */
static void check_delayed_start(bool previous_valid, bool miss_busy)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(ens210));
	struct ens210_emul_data *data = EMUL_DT_GET(DT_NODELABEL(ens210))->data;
	enum sensor_channel channel;
	struct sensor_value value;
	int64_t start;

	if (!(ENS210_T_RUN || ENS210_H_RUN) || single_shot_mask(SENSOR_CHAN_ALL) == 0) {
		ztest_test_skip();
		return;
	}
	channel = ENS210_T_RUN ? SENSOR_CHAN_HUMIDITY : SENSOR_CHAN_AMBIENT_TEMP;
	data->delayed_start = true;
	data->previous_valid = previous_valid;
	/*
	 * Fig. 9 maxima: T-only continuous 109 ms, combined continuous 238 ms.
	 * Bound RH-only conservatively by the combined step. The shorter case
	 * finishes before polling and does not require observing busy.
	 */
	data->start_delay_ms = miss_busy ? 100 : (ENS210_T_RUN ? 109 : 238);
	data->conversion_ms = miss_busy ? 130 : 238;
	start = k_uptime_get();
	zassert_ok(sensor_sample_fetch_chan(dev, channel));
	zassert_between_inclusive(k_uptime_get() - start,
		MAX(238, data->start_delay_ms + data->conversion_ms), 530);
	zassert_equal(data->start_mask, single_shot_mask(channel));
	zassert_equal(data->value_reads, 1);
	zassert_ok(sensor_channel_get(dev, channel, &value));
	if (channel == SENSOR_CHAN_AMBIENT_TEMP) {
		/* New raw temperature 19000 / 64 K, minus 273.15. */
		zassert_equal(sensor_value_to_micro(&value), 23725000);
	} else {
		/* New raw humidity 25000 / 512 percent. */
		zassert_equal(sensor_value_to_micro(&value), 48828125);
	}
	if (miss_busy) {
		zassert_equal(data->status_reads, 1);
	} else {
		zassert_true(data->status_reads > 1);
	}
}

ZTEST(ens210, test_delayed_start_invalid)
{
	check_delayed_start(false, false);
}

ZTEST(ens210, test_delayed_start_stale)
{
	check_delayed_start(true, false);
}

ZTEST(ens210, test_delayed_start_missed_busy)
{
	check_delayed_start(true, true);
}

static void *ens210_setup(void)
{
	zassert_ok(device_init(DEVICE_DT_GET(DT_NODELABEL(ens210))));
	return NULL;
}

ZTEST_SUITE(ens210, NULL, ens210_setup, ens210_before, NULL, NULL);
