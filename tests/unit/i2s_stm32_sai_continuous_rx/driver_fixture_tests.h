/*
 * Copyright (c) 2026 Hula Firmware
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef I2S_STM32_SAI_DRIVER_FIXTURE_TESTS_H_
#define I2S_STM32_SAI_DRIVER_FIXTURE_TESTS_H_

K_MEM_SLAB_DEFINE(fixture_slab, 16U, SAI_CONT_RX_NODE_COUNT, 4U);

static struct stm32_sai_sub_data fixture_data;
static struct stm32_sai_sub_cfg fixture_config = {.dir = I2S_DIR_RX};
static struct device fixture_device = {
	.name = "sai-fixture",
	.config = &fixture_config,
	.data = &fixture_data,
};

static void fixture_init(void)
{
	memset(&fixture_data, 0, sizeof(fixture_data));
	memset(&fixture_node_storage, 0, sizeof(fixture_node_storage));
	memset(&fixture_dma_registers, 0, sizeof(fixture_dma_registers));
	memset(&fixture_sai_registers, 0, sizeof(fixture_sai_registers));
	fixture_deinit_fails = false;
	fixture_deinit_count = 0U;
	fixture_data.continuous_node_storage = fixture_node_storage;
	fixture_data.hdma.Instance = &fixture_dma_registers;
	fixture_data.hsai.Instance = &fixture_sai_registers;
	fixture_data.hsai.State = HAL_SAI_STATE_BUSY_RX;
	fixture_data.stream.state = I2S_STATE_RUNNING;
	fixture_data.stream.continuous_rx = true;
	fixture_data.stream.i2s_cfg.block_size = 16U;
	fixture_data.stream.i2s_cfg.timeout = 0;
	fixture_data.stream.i2s_cfg.mem_slab = &fixture_slab;
	fixture_data.stream.dma_src_size = 2U;
	fixture_data.stream.stream_start = stream_start;
	fixture_data.stream.queue_drop = queue_drop;
	k_msgq_init(&fixture_data.stream.queue, (char *)fixture_data.queue_buffer,
		    sizeof(struct queue_item), ARRAY_SIZE(fixture_data.queue_buffer));
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		fixture_data.stream.continuous_nodes[i] = &fixture_node_storage[i].node;
		zassert_ok(k_mem_slab_alloc(&fixture_slab,
					    &fixture_data.stream.continuous_blocks[i], K_NO_WAIT));
		fixture_data.stream.continuous_node_state[i] = SAI_CONT_RX_NODE_DMA;
	}
	fixture_data.stream.continuous_tail = fixture_data.stream.continuous_nodes[7];
	fixture_data.stream.continuous_link_mask = SAI_CONT_RX_LINK_UPDATE_MASK;
	fixture_dma_registers.CCR = DMA_CCR_EN;
	fixture_sai_registers.CR1 = SAI_xCR1_DMAEN | SAI_xCR1_SAIEN;
}

static void fixture_callback(uint32_t flags, uintptr_t current)
{
	fixture_data.hdma.flags = flags;
	fixture_dma_registers.CDAR = current;
	dma_callback(NULL, &fixture_data.hdma, 0U, 0);
}

static void fixture_cleanup(void)
{
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		if (fixture_data.stream.continuous_blocks[i] != NULL) {
			k_mem_slab_free(&fixture_slab, fixture_data.stream.continuous_blocks[i]);
			fixture_data.stream.continuous_blocks[i] = NULL;
		}
	}
}

ZTEST(i2s_stm32_sai_continuous_rx, test_delayed_tc_stop_publishes_prefix_and_restarts_safely)
{
	void *first;
	void *second;
	size_t size;

	fixture_init();
	fixture_dma_registers.CDAR = (uintptr_t)fixture_data.stream.continuous_blocks[0] + 8U;
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_STOP));
	zassert_equal(I2S_STATE_STOPPING, fixture_data.stream.state);
	zassert_true(fixture_data.stream.continuous_stop_pending);

	fixture_callback(DMA_FLAG_TC, (uintptr_t)fixture_data.stream.continuous_blocks[2] + 4U);
	zassert_equal(I2S_STATE_READY, fixture_data.stream.state);
	zassert_false(fixture_data.stream.continuous_stop_pending);
	zassert_true(fixture_data.stream.continuous_stopped);
	zassert_false((fixture_dma_registers.CCR & DMA_CCR_EN) != 0U);
	zassert_equal(2U, k_msgq_num_used_get(&fixture_data.stream.queue));
	zassert_equal(SAI_CONT_RX_NODE_QUEUED, fixture_data.stream.continuous_node_state[0]);
	zassert_equal(SAI_CONT_RX_NODE_QUEUED, fixture_data.stream.continuous_node_state[1]);
	zassert_equal(SAI_CONT_RX_NODE_DMA, fixture_data.stream.continuous_node_state[2]);

	zassert_ok(stm32_sai_sub_read(&fixture_device, &first, &size));
	zassert_equal(16U, size);
	zassert_equal(fixture_data.stream.continuous_blocks[0], first);
	zassert_ok(stm32_sai_sub_read(&fixture_device, &second, &size));
	zassert_equal(fixture_data.stream.continuous_blocks[1], second);

	fixture_dma_registers.CSR |= DMA_CSR_IDLEF;
	zassert_equal(-EBUSY,
		      stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	zassert_equal(I2S_STATE_READY, fixture_data.stream.state);
	zassert_equal(-EINVAL, stm32_sai_rx_release(&fixture_device, second));
	zassert_ok(stm32_sai_rx_release(&fixture_device, first));
	zassert_ok(stm32_sai_rx_release(&fixture_device, second));
	zassert_equal(2U, fixture_data.stream.continuous_next_release);
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	zassert_equal(I2S_STATE_RUNNING, fixture_data.stream.state);
	fixture_cleanup();
}

ZTEST(i2s_stm32_sai_continuous_rx, test_drain_ignores_half_transfer_until_target_tc)
{
	fixture_init();
	fixture_dma_registers.CDAR = (uintptr_t)fixture_data.stream.continuous_blocks[0] + 8U;
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_DRAIN));
	fixture_callback(DMA_FLAG_HT, (uintptr_t)fixture_data.stream.continuous_blocks[0] + 12U);
	zassert_equal(I2S_STATE_STOPPING, fixture_data.stream.state);
	zassert_true(fixture_data.stream.continuous_stop_pending);
	zassert_equal(0U, k_msgq_num_used_get(&fixture_data.stream.queue));
	zassert_true((fixture_dma_registers.CCR & DMA_CCR_EN) != 0U);

	fixture_callback(DMA_FLAG_TC, (uintptr_t)fixture_data.stream.continuous_blocks[1]);
	zassert_equal(I2S_STATE_READY, fixture_data.stream.state);
	zassert_equal(1U, k_msgq_num_used_get(&fixture_data.stream.queue));
	zassert_equal(SAI_CONT_RX_NODE_QUEUED, fixture_data.stream.continuous_node_state[0]);
	fixture_cleanup();
}

ZTEST(i2s_stm32_sai_continuous_rx, test_stop_remembers_target_published_by_half_transfer)
{
	fixture_init();
	fixture_dma_registers.CDAR = (uintptr_t)fixture_data.stream.continuous_blocks[0] + 8U;
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_STOP));
	fixture_callback(DMA_FLAG_HT,
			 (uintptr_t)fixture_data.stream.continuous_blocks[1] + 4U);
	zassert_equal(I2S_STATE_STOPPING, fixture_data.stream.state);
	zassert_true(fixture_data.stream.continuous_stop_target_complete);
	zassert_equal(SAI_CONT_RX_NODE_QUEUED, fixture_data.stream.continuous_node_state[0]);
	zassert_true((fixture_dma_registers.CCR & DMA_CCR_EN) != 0U);

	fixture_callback(DMA_FLAG_TC,
			 (uintptr_t)fixture_data.stream.continuous_blocks[2] + 4U);
	zassert_equal(I2S_STATE_READY, fixture_data.stream.state);
	zassert_equal(2U, k_msgq_num_used_get(&fixture_data.stream.queue));
	fixture_cleanup();
}

ZTEST(i2s_stm32_sai_continuous_rx, test_stop_terminal_tail_publishes_completed_ring)
{
	fixture_init();
	fixture_dma_registers.CDAR = (uintptr_t)fixture_data.stream.continuous_blocks[7] + 8U;
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_STOP));
	fixture_dma_registers.CCR &= ~DMA_CCR_EN;
	fixture_dma_registers.CSR |= DMA_CSR_IDLEF;
	fixture_callback(DMA_FLAG_TC,
			 (uintptr_t)fixture_data.stream.continuous_blocks[7] + 16U);
	zassert_equal(I2S_STATE_READY, fixture_data.stream.state);
	zassert_equal(SAI_CONT_RX_NODE_COUNT, k_msgq_num_used_get(&fixture_data.stream.queue));
	zassert_equal(SAI_CONT_RX_NODE_QUEUED, fixture_data.stream.continuous_node_state[7]);
	fixture_cleanup();
}

ZTEST(i2s_stm32_sai_continuous_rx, test_tail_address_alias_does_not_publish_active_block)
{
	void *saved_block0;
	uintptr_t tail_end;

	fixture_init();
	fixture_dma_registers.CDAR = (uintptr_t)fixture_data.stream.continuous_blocks[0] + 8U;
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_STOP));
	saved_block0 = fixture_data.stream.continuous_blocks[0];
	tail_end = (uintptr_t)fixture_data.stream.continuous_blocks[7] + 16U;
	fixture_data.stream.continuous_blocks[0] = (void *)tail_end;
	fixture_callback(DMA_FLAG_TC, tail_end);
	zassert_equal(I2S_STATE_STOPPING, fixture_data.stream.state);
	zassert_true(fixture_data.stream.continuous_stop_pending);
	zassert_equal(0U, k_msgq_num_used_get(&fixture_data.stream.queue));
	zassert_equal(SAI_CONT_RX_NODE_DMA, fixture_data.stream.continuous_node_state[7]);
	fixture_data.stream.continuous_blocks[0] = saved_block0;
	fixture_cleanup();
}

ZTEST(i2s_stm32_sai_continuous_rx, test_drop_during_stop_discards_without_stop_transition)
{
	fixture_init();
	fixture_dma_registers.CDAR = (uintptr_t)fixture_data.stream.continuous_blocks[0] + 8U;
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_STOP));
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_DROP));
	zassert_equal(I2S_STATE_READY, fixture_data.stream.state);
	zassert_false(fixture_data.stream.continuous_stop_pending);
	zassert_true(fixture_data.stream.continuous_quiesced);
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		zassert_is_null(fixture_data.stream.continuous_blocks[i]);
		zassert_equal(SAI_CONT_RX_NODE_FREE, fixture_data.stream.continuous_node_state[i]);
	}
	fixture_cleanup();
}

ZTEST(i2s_stm32_sai_continuous_rx, test_drop_keeps_borrowed_buffers_until_release)
{
	void *first;
	void *second;
	size_t size;

	fixture_init();
	fixture_dma_registers.CDAR = (uintptr_t)fixture_data.stream.continuous_blocks[0] + 8U;
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_STOP));
	fixture_callback(DMA_FLAG_TC,
			 (uintptr_t)fixture_data.stream.continuous_blocks[2] + 4U);
	zassert_equal(I2S_STATE_READY, fixture_data.stream.state);
	zassert_ok(stm32_sai_sub_read(&fixture_device, &first, &size));
	zassert_ok(stm32_sai_sub_read(&fixture_device, &second, &size));
	fixture_dma_registers.CSR |= DMA_CSR_IDLEF;
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_DROP));
	zassert_true(fixture_data.stream.continuous_quiesced);
	zassert_equal(SAI_CONT_RX_NODE_BORROWED, fixture_data.stream.continuous_node_state[0]);
	zassert_equal(SAI_CONT_RX_NODE_BORROWED, fixture_data.stream.continuous_node_state[1]);
	zassert_equal(first, fixture_data.stream.continuous_blocks[0]);
	zassert_equal(second, fixture_data.stream.continuous_blocks[1]);
	zassert_equal(-EBUSY,
		      stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	zassert_equal(-EINVAL, stm32_sai_rx_release(&fixture_device, second));
	zassert_ok(stm32_sai_rx_release(&fixture_device, first));
	zassert_ok(stm32_sai_rx_release(&fixture_device, second));
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	zassert_equal(I2S_STATE_RUNNING, fixture_data.stream.state);
	fixture_cleanup();
}

ZTEST(i2s_stm32_sai_continuous_rx, test_drop_discards_queued_after_failed_restart)
{
	void *borrowed;
	size_t size;
	unsigned int deinit_count;

	fixture_init();
	fixture_dma_registers.CDAR = (uintptr_t)fixture_data.stream.continuous_blocks[0] + 8U;
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_STOP));
	fixture_callback(DMA_FLAG_TC,
			 (uintptr_t)fixture_data.stream.continuous_blocks[2] + 4U);
	zassert_ok(stm32_sai_sub_read(&fixture_device, &borrowed, &size));
	fixture_dma_registers.CSR |= DMA_CSR_IDLEF;
	zassert_equal(-EBUSY,
		      stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	deinit_count = fixture_deinit_count;
	zassert_equal(deinit_count, fixture_deinit_count);
	zassert_equal(1U, k_msgq_num_used_get(&fixture_data.stream.queue));

	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_DROP));
	zassert_equal(deinit_count, fixture_deinit_count);
	zassert_equal(0U, k_msgq_num_used_get(&fixture_data.stream.queue));
	zassert_equal(SAI_CONT_RX_NODE_BORROWED,
		      fixture_data.stream.continuous_node_state[0]);
	zassert_is_null(fixture_data.stream.continuous_blocks[1]);
	zassert_equal(-EBUSY,
		      stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	zassert_ok(stm32_sai_rx_release(&fixture_device, borrowed));
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	zassert_equal(I2S_STATE_RUNNING, fixture_data.stream.state);
	fixture_cleanup();
}

ZTEST(i2s_stm32_sai_continuous_rx, test_deinit_failure_preserves_dma_blocks_for_prepare)
{
	fixture_init();
	fixture_deinit_fails = true;
	zassert_equal(-EIO,
		      stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_DROP));
	zassert_equal(I2S_STATE_ERROR, fixture_data.stream.state);
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		zassert_not_null(fixture_data.stream.continuous_blocks[i]);
		zassert_equal(SAI_CONT_RX_NODE_DMA, fixture_data.stream.continuous_node_state[i]);
	}

	fixture_deinit_fails = false;
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_PREPARE));
	zassert_equal(I2S_STATE_READY, fixture_data.stream.state);
	zassert_true(fixture_data.stream.continuous_quiesced);
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	zassert_equal(I2S_STATE_RUNNING, fixture_data.stream.state);
	fixture_cleanup();
}

#endif /* I2S_STM32_SAI_DRIVER_FIXTURE_TESTS_H_ */
