/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_ENS210_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_ENS210_H_
#include <zephyr/device.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/** Read the device identity freshly. Caller serializes access to this device. */
int ens210_get_part_id(const struct device *dev, uint16_t *id);
#ifdef __cplusplus
}
#endif
#endif
