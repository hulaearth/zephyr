/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_I2S_STM32_SAI_H_
#define ZEPHYR_INCLUDE_DRIVERS_I2S_STM32_SAI_H_

#include <zephyr/device.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Release one completed receive buffer to the STM32N6 SAI DMA list.
 *
 * @param dev SAI device that returned the buffer.
 * @param buffer Buffer returned by @ref i2s_read.
 *
 * @retval 0 Buffer released.
 * @retval -EINVAL Buffer is invalid or released out of order.
 * @retval -EIO Continuous receive is in a fault state.
 * @retval -ENOTSUP Continuous receive is not configured.
 */
int stm32_sai_rx_release(const struct device *dev, void *buffer);

/**
 * @brief Read continuous receive queue and slab high-water marks.
 *
 * @param dev SAI device.
 * @param queue_high_water Receives the highest number of queued buffers.
 * @param queue_capacity Receives the queue capacity.
 * @param slab_high_water Receives the highest number of allocated slabs.
 * @param slab_capacity Receives the slab capacity.
 *
 * @retval 0 Statistics returned.
 * @retval -EINVAL At least one pointer is null.
 * @retval -ENOTSUP Continuous receive is not configured.
 */
int stm32_sai_rx_get_stats(const struct device *dev, uint32_t *queue_high_water,
			   uint32_t *queue_capacity, uint32_t *slab_high_water,
			   uint32_t *slab_capacity);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_I2S_STM32_SAI_H_ */
