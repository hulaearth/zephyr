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

/** @brief Software descriptor progress since the last continuous RX start. */
struct stm32_sai_rx_evidence {
	uint32_t append_count; /**< Descriptor link writes attempted. */
	uint32_t append_readback_failures; /**< Link writes with a mismatched readback. */
	uint32_t appended_completions; /**< Completed descriptors appended after start. */
	uint32_t completion_count; /**< Completed descriptors queued for the caller. */
	uint32_t completion_callbacks; /**< DMA callbacks that publish descriptors. */
	uint32_t coalesced_callbacks; /**< Callbacks that publish multiple descriptors. */
	uint32_t max_callback_gap_cycles; /**< Largest gap in Zephyr cycle-clock units. */
};

/**
 * @brief Snapshot software descriptor progress without changing buffer ownership.
 *
 * Counters wrap modulo 2^32 and survive stop. They reset on the next start.
 * These counters do not prove sample continuity or a hard timing bound.
 *
 * @param dev SAI device. Must not be NULL.
 * @param evidence Caller-owned snapshot destination. Must not be NULL.
 *
 * @retval 0 Snapshot returned.
 * @retval -EINVAL At least one pointer is null.
 * @retval -ENOTSUP Continuous receive is not configured.
 */
int stm32_sai_rx_get_evidence(const struct device *dev, struct stm32_sai_rx_evidence *evidence);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_I2S_STM32_SAI_H_ */
