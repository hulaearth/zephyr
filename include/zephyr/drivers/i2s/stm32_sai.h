/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Hula Firmware
 *
 * SPDX-License-Identifier: Apache-2.0
 */
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

/** @brief First observed continuous RX failure site. */
enum stm32_sai_rx_fault_reason {
	STM32_SAI_RX_FAULT_NONE = 0,
	STM32_SAI_RX_FAULT_DMA,
	STM32_SAI_RX_FAULT_PREFIX,
	STM32_SAI_RX_FAULT_QUEUE,
	STM32_SAI_RX_FAULT_READ_SAI,
	STM32_SAI_RX_FAULT_READ_OWNER,
	STM32_SAI_RX_FAULT_RELEASE_IDLE,
	STM32_SAI_RX_FAULT_START,
	STM32_SAI_RX_FAULT_QUIESCE,
};

/**
 * @brief First failure observed before the driver clears flags or stops DMA.
 *
 * Registers are sequential reads, not an atomic hardware trace. dma_flags
 * retains callback-entry flags even when prefix or queue checks follow an
 * acknowledgment. No PCM or buffer contents are copied.
 */
struct stm32_sai_rx_fault {
	uint32_t reason;           /**< enum stm32_sai_rx_fault_reason; NONE means no fault. */
	int32_t status;            /**< Callback status or failing operation result. */
	uint32_t cycle;            /**< Zephyr cycle clock at the first observed failure. */
	int32_t stream_state;      /**< I2S state before failure cleanup. */
	uint32_t sai_sr;           /**< SAI status register. */
	uint32_t sai_cr1;          /**< SAI control register. */
	uint32_t sai_error;        /**< HAL SAI error code before cleanup. */
	uint32_t dma_flags;        /**< DMA flags observed by the failure path. */
	uint32_t dma_csr;          /**< DMA channel status register. */
	uint32_t dma_ccr;          /**< DMA channel control register. */
	uint32_t dma_cbr1;         /**< DMA block register, including remaining byte count. */
	uint32_t dma_csar;         /**< Current DMA source address. */
	uint32_t dma_cdar;         /**< Current DMA destination address. */
	uint32_t dma_cllr;         /**< Current linked-list register. */
	uint32_t dma_error;        /**< HAL DMA error code before cleanup. */
	uint32_t completion_count; /**< Published buffers at failure. */
	uint32_t append_count;     /**< Attempted descriptor appends at failure. */
	uint32_t node_states;      /**< Eight node owners, two bits per node, node 0 first. */
	uint32_t next_complete;    /**< Next descriptor expected to complete. */
	uint32_t next_release;     /**< Next borrowed descriptor expected back. */
};

/** @brief Software descriptor progress since the last continuous RX start. */
struct stm32_sai_rx_evidence {
	struct stm32_sai_rx_fault first_fault; /**< Preserved until the next RX start. */
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
 * The first fault also survives stop, DROP and PREPARE. A rejected start that
 * still has owned buffers does not clear it. Read it before restarting RX.
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
