/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_LSM6DSO_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_LSM6DSO_H_
#include <zephyr/device.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/** Read the device identity freshly. Caller serializes access to this device. */
int lsm6dso_get_who_am_i(const struct device *dev, uint8_t *id);
#ifdef __cplusplus
}
#endif
#endif
