/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_DRIVERS_I2S_STM32_SAI_RX_RING_H_
#define ZEPHYR_DRIVERS_I2S_STM32_SAI_RX_RING_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum sai_rx_prefix_result {
	SAI_RX_PREFIX_FAULT = -1,
	SAI_RX_PREFIX_DEFER = 0,
	SAI_RX_PREFIX_READY = 1,
};

static inline enum sai_rx_prefix_result
sai_rx_plan_completed_prefix(uint8_t next, int8_t active, uint8_t node_count,
			     const uint8_t *states, uint8_t dma_state,
			     bool boundary, bool channel_enabled,
			     bool terminal_end, uint8_t *publish_count)
{
	uint8_t count = 0U;
	uint8_t candidate = next;

	*publish_count = 0U;
	if (active < 0) {
		if (terminal_end) {
			return SAI_RX_PREFIX_FAULT;
		}
		return boundary && channel_enabled ? SAI_RX_PREFIX_DEFER : SAI_RX_PREFIX_FAULT;
	}
	if ((uint8_t)active >= node_count || node_count == 0U || states[(uint8_t)active] != dma_state) {
		return SAI_RX_PREFIX_FAULT;
	}
	while (candidate != (uint8_t)active && count < node_count) {
		if (states[candidate] != dma_state) {
			return SAI_RX_PREFIX_FAULT;
		}
		candidate = (uint8_t)((candidate + 1U) % node_count);
		count++;
	}
	if (candidate != (uint8_t)active) {
		return SAI_RX_PREFIX_FAULT;
	}
	*publish_count = count;
	return SAI_RX_PREFIX_READY;
}

static inline enum sai_rx_prefix_result
sai_rx_plan_prefix_through(uint8_t next, uint8_t end, uint8_t node_count,
			   const uint8_t *states, uint8_t dma_state,
			   uint8_t *publish_count)
{
	uint8_t count = 0U;
	uint8_t candidate = next;

	*publish_count = 0U;
	if (node_count == 0U || next >= node_count || end >= node_count) {
		return SAI_RX_PREFIX_FAULT;
	}
	while (count < node_count) {
		if (states[candidate] != dma_state) {
			return SAI_RX_PREFIX_FAULT;
		}
		count++;
		if (candidate == end) {
			*publish_count = count;
			return SAI_RX_PREFIX_READY;
		}
		candidate = (uint8_t)((candidate + 1U) % node_count);
	}
	return SAI_RX_PREFIX_FAULT;
}

static inline bool sai_rx_prefix_contains(uint8_t next, uint8_t count,
					  uint8_t node_count, uint8_t target)
{
	if (node_count == 0U || next >= node_count || target >= node_count || count > node_count) {
		return false;
	}
	while (count > 0U) {
		if (next == target) {
			return true;
		}
		next = (uint8_t)((next + 1U) % node_count);
		count--;
	}
	return false;
}

static inline bool sai_rx_release_is_ordered(uint8_t index, uint8_t next_release,
					     uint8_t state, uint8_t borrowed_state,
					     bool pointer_matches)
{
	return pointer_matches && state == borrowed_state && index == next_release;
}

/* Apply the bookkeeping transition for a borrowed buffer released after DMA stops. */
static inline bool sai_rx_release_quiesced(uint8_t index, uint8_t node_count,
					   uint8_t *next_release, uint8_t *states,
					   uint8_t borrowed_state, uint8_t free_state,
					   void **blocks, void *buffer)
{
	if (node_count == 0U || index >= node_count || index != *next_release ||
	    states[index] != borrowed_state || blocks[index] != buffer || buffer == NULL) {
		return false;
	}
	blocks[index] = NULL;
	states[index] = free_state;
	*next_release = (uint8_t)((index + 1U) % node_count);
	return true;
}

#endif /* ZEPHYR_DRIVERS_I2S_STM32_SAI_RX_RING_H_ */
