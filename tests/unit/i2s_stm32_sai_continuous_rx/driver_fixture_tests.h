/*
 * Copyright (c) 2026 Hula Firmware
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef I2S_STM32_SAI_DRIVER_FIXTURE_TESTS_H_
#define I2S_STM32_SAI_DRIVER_FIXTURE_TESTS_H_

K_MEM_SLAB_DEFINE(fixture_slab, 16U, SAI_CONT_RX_NODE_COUNT, 4U);
K_MEM_SLAB_DEFINE(fixture_continuous_slab, 16384U, SAI_CONT_RX_NODE_COUNT, 32U);

static int fixture_dma_config(const struct device *dev, uint32_t channel,
			      struct dma_config *config)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel);
	ARG_UNUSED(config);
	return 0;
}

static DEVICE_API(dma, fixture_dma_api) = {
	.config = fixture_dma_config,
};

static struct stm32_sai_sub_data fixture_data;
static struct stm32_sai_sub_cfg fixture_config = {.dir = I2S_DIR_RX};
static struct device_state fixture_device_state = {.initialized = true};
static struct device fixture_device = {
	.name = "sai-fixture",
	.api = &fixture_dma_api,
	.state = &fixture_device_state,
	.config = &fixture_config,
	.data = &fixture_data,
};

static void fixture_empty_init(void)
{
	memset(&fixture_data, 0, sizeof(fixture_data));
	memset(&fixture_node_storage, 0, sizeof(fixture_node_storage));
	memset(&fixture_dma_registers, 0, sizeof(fixture_dma_registers));
	fixture_dma_registers.CSR = DMA_CSR_IDLEF;
	memset(&fixture_sai_registers, 0, sizeof(fixture_sai_registers));
	fixture_deinit_fails = false;
	fixture_list_init_fails = false;
	fixture_list_start_fails = false;
	fixture_list_start_fails_after_enable = false;
	fixture_deinit_count = 0U;
	fixture_list_start_count = 0U;
	fixture_normal_start_count = 0U;
	fixture_sai_rx_used_linked_list = false;
	fixture_normal_start_destination = NULL;
	fixture_sai_rx_linked_list_count = 0U;
	fixture_sai_rx_stale_queue_start_count = 0U;
	fixture_data.continuous_node_storage = fixture_node_storage;
	fixture_data.hdma.Instance = &fixture_dma_registers;
	fixture_data.hsai.Instance = &fixture_sai_registers;
	fixture_data.hsai.hdmarx = &fixture_data.hdma;
	fixture_data.hsai.State = HAL_SAI_STATE_BUSY_RX;
	fixture_data.hdma.Parent = &fixture_data.hsai;
	fixture_data.stream.state = I2S_STATE_NOT_READY;
	fixture_data.stream.continuous_rx = false;
	fixture_data.stream.i2s_cfg.block_size = 16U;
	fixture_data.stream.i2s_cfg.timeout = 0;
	fixture_data.stream.i2s_cfg.mem_slab = &fixture_slab;
	fixture_data.stream.dma_src_size = 2U;
	fixture_data.stream.dma_dev = &fixture_device;
	fixture_data.stream.reg = (DMA_TypeDef *)&fixture_dma_registers;
	fixture_data.stream.stream_start = stream_start;
	fixture_data.stream.queue_drop = queue_drop;
	fixture_data.hdma.Init.Mode = DMA_NORMAL;
	fixture_data.hdma.Mode = DMA_NORMAL;
	fixture_data.hdma.State = HAL_DMA_STATE_READY;
	k_msgq_init(&fixture_data.stream.queue, (char *)fixture_data.queue_buffer,
		    sizeof(struct queue_item), ARRAY_SIZE(fixture_data.queue_buffer));
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		fixture_data.stream.continuous_nodes[i] = &fixture_node_storage[i].node;
	}
	fixture_data.stream.continuous_tail = fixture_data.stream.continuous_nodes[7];
	fixture_data.stream.continuous_link_mask = SAI_CONT_RX_LINK_UPDATE_MASK;
}

static struct i2s_config fixture_continuous_config(void)
{
	return (struct i2s_config){
		.word_size = 16U,
		.channels = 2U,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_BIT_CLK_GATED,
		.frame_clk_freq = 384000U,
		.mem_slab = &fixture_continuous_slab,
		.block_size = 16384U,
		.timeout = 0,
	};
}

static struct i2s_config fixture_normal_config(void)
{
	return (struct i2s_config){
		.word_size = 16U,
		.channels = 2U,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_BIT_CLK_GATED,
		.frame_clk_freq = 48000U,
		.mem_slab = &fixture_slab,
		.block_size = 16U,
		.timeout = 0,
	};
}

static void fixture_init(void)
{
	fixture_empty_init();
	fixture_data.stream.state = I2S_STATE_RUNNING;
	fixture_data.stream.continuous_rx = true;
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		zassert_ok(k_mem_slab_alloc(&fixture_slab,
					    &fixture_data.stream.continuous_blocks[i], K_NO_WAIT));
		fixture_data.stream.continuous_node_state[i] = SAI_CONT_RX_NODE_DMA;
	}
	fixture_dma_registers.CCR = DMA_CCR_EN;
	fixture_sai_registers.CR1 = SAI_xCR1_DMAEN | SAI_xCR1_SAIEN;
}

static void fixture_callback(uint32_t flags, uintptr_t current)
{
	fixture_data.hdma.flags = flags;
	fixture_dma_registers.CDAR = current;
	dma_callback(NULL, &fixture_data.hdma, 0U, 0);
}

static void fixture_complete_dma_stop(void)
{
	fixture_dma_registers.CCR &= ~DMA_CCR_EN;
	fixture_dma_registers.CSR |= DMA_CSR_IDLEF;
}

static void fixture_drop_and_restart_reversed(void)
{
	void *initial_blocks[SAI_CONT_RX_NODE_COUNT];

	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		initial_blocks[i] = fixture_data.stream.continuous_blocks[i];
	}
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_DROP));
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		zassert_equal(initial_blocks[SAI_CONT_RX_NODE_COUNT - i - 1U],
			      fixture_data.stream.continuous_blocks[i]);
	}
}

static void fixture_cleanup(void)
{
	if (!fixture_data.stream.continuous_rx && fixture_data.stream.mem_block != NULL) {
		k_mem_slab_free(fixture_data.stream.i2s_cfg.mem_slab,
				fixture_data.stream.mem_block);
		fixture_data.stream.mem_block = NULL;
	}
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		if (fixture_data.stream.continuous_blocks[i] != NULL) {
			k_mem_slab_free(&fixture_slab, fixture_data.stream.continuous_blocks[i]);
			fixture_data.stream.continuous_blocks[i] = NULL;
		}
	}
}

ZTEST(i2s_stm32_sai_continuous_rx, test_descriptor_evidence_tracks_append_and_completion)
{
	struct stm32_sai_rx_evidence evidence;
	void *first;
	void *second;
	size_t size;

	fixture_init();
	zassert_equal(-EINVAL, stm32_sai_rx_get_evidence(NULL, &evidence));
	zassert_equal(-EINVAL, stm32_sai_rx_get_evidence(&fixture_device, NULL));
	fixture_data.stream.continuous_rx = false;
	zassert_equal(-ENOTSUP, stm32_sai_rx_get_evidence(&fixture_device, &evidence));
	fixture_data.stream.continuous_rx = true;
	fixture_callback(DMA_FLAG_TC, (uintptr_t)fixture_data.stream.continuous_blocks[2] + 4U);
	zassert_ok(stm32_sai_sub_read(&fixture_device, &first, &size));
	zassert_ok(stm32_sai_sub_read(&fixture_device, &second, &size));
	zassert_ok(stm32_sai_rx_release(&fixture_device, first));
	zassert_ok(stm32_sai_rx_release(&fixture_device, second));
	fixture_callback(DMA_FLAG_TC, (uintptr_t)fixture_data.stream.continuous_blocks[0] + 4U);
	fixture_callback(DMA_FLAG_TC, (uintptr_t)fixture_data.stream.continuous_blocks[1] + 4U);
	zassert_ok(stm32_sai_rx_get_evidence(&fixture_device, &evidence));
	zassert_equal(2U, evidence.append_count);
	zassert_equal(0U, evidence.append_readback_failures);
	zassert_equal(1U, evidence.appended_completions);
	zassert_equal(9U, evidence.completion_count);
	zassert_equal(3U, evidence.completion_callbacks);
	zassert_equal(2U, evidence.coalesced_callbacks);
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_DROP));
	zassert_ok(stm32_sai_rx_get_evidence(&fixture_device, &evidence));
	zassert_equal(9U, evidence.completion_count);
	zassert_equal(2U, evidence.append_count);
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	zassert_ok(stm32_sai_rx_get_evidence(&fixture_device, &evidence));
	zassert_equal(0U, evidence.completion_count);
	zassert_equal(0U, evidence.append_count);
	zassert_equal(0U, evidence.completion_callbacks);
	zassert_equal(0U, evidence.max_callback_gap_cycles);
	fixture_cleanup();
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
	zassert_true((fixture_dma_registers.CCR & DMA_CCR_EN) != 0U);
	zassert_equal(DMA_CCR_SUSP | DMA_CCR_RESET,
		      fixture_dma_registers.CCR & (DMA_CCR_SUSP | DMA_CCR_RESET));
	zassert_equal(2U, k_msgq_num_used_get(&fixture_data.stream.queue));
	zassert_equal(SAI_CONT_RX_NODE_QUEUED, fixture_data.stream.continuous_node_state[0]);
	zassert_equal(SAI_CONT_RX_NODE_QUEUED, fixture_data.stream.continuous_node_state[1]);
	zassert_equal(SAI_CONT_RX_NODE_DMA, fixture_data.stream.continuous_node_state[2]);

	zassert_ok(stm32_sai_sub_read(&fixture_device, &first, &size));
	zassert_equal(16U, size);
	zassert_equal(fixture_data.stream.continuous_blocks[0], first);
	zassert_ok(stm32_sai_sub_read(&fixture_device, &second, &size));
	zassert_equal(fixture_data.stream.continuous_blocks[1], second);

	fixture_complete_dma_stop();
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
	zassert_true((fixture_dma_registers.CCR & DMA_CCR_EN) != 0U);
	zassert_equal(DMA_CCR_SUSP | DMA_CCR_RESET,
		      fixture_dma_registers.CCR & (DMA_CCR_SUSP | DMA_CCR_RESET));
	fixture_cleanup();
}

static void fixture_publish_prior_tail_blocks_and_request_stop(void **borrowed)
{
	size_t size;
	uintptr_t tail_end = (uintptr_t)fixture_data.stream.continuous_blocks[7] + 16U;

	fixture_callback(DMA_FLAG_HT,
			 (uintptr_t)fixture_data.stream.continuous_blocks[7] + 4U);
	zassert_equal(7U, k_msgq_num_used_get(&fixture_data.stream.queue));
	zassert_ok(stm32_sai_sub_read(&fixture_device, borrowed, &size));
	zassert_equal(fixture_data.stream.continuous_blocks[0], *borrowed);
	fixture_dma_registers.CDAR = tail_end;
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_STOP));
	zassert_equal(7U, fixture_data.stream.continuous_stop_target);
	zassert_false(fixture_data.stream.continuous_stop_target_complete);
}

ZTEST(i2s_stm32_sai_continuous_rx, test_stop_alias_fails_safe_without_terminal_proof)
{
	void *borrowed;
	uintptr_t tail_end;

	fixture_init();
	fixture_drop_and_restart_reversed();
	tail_end = (uintptr_t)fixture_data.stream.continuous_blocks[7] + 16U;
	fixture_publish_prior_tail_blocks_and_request_stop(&borrowed);
	zassert_equal((uintptr_t)fixture_data.stream.continuous_blocks[6], tail_end);
	zassert_equal(SAI_CONT_RX_NODE_BORROWED,
		      fixture_data.stream.continuous_node_state[0]);

	/* EN remains set, so CDAR alone cannot prove the finite tail completed. */
	fixture_callback(DMA_FLAG_TC, tail_end);
	zassert_equal(I2S_STATE_ERROR, fixture_data.stream.state);
	zassert_true(fixture_data.stream.continuous_fault);
	zassert_equal(6U, k_msgq_num_used_get(&fixture_data.stream.queue));
	zassert_equal(SAI_CONT_RX_NODE_BORROWED,
		      fixture_data.stream.continuous_node_state[0]);
	zassert_equal(SAI_CONT_RX_NODE_DMA,
		      fixture_data.stream.continuous_node_state[7]);
	zassert_equal(DMA_CCR_SUSP | DMA_CCR_RESET,
		      fixture_dma_registers.CCR & (DMA_CCR_SUSP | DMA_CCR_RESET));
	zassert_true((fixture_dma_registers.CCR & DMA_CCR_EN) != 0U);
	zassert_equal(borrowed, fixture_data.stream.continuous_blocks[0]);
	fixture_cleanup();
}

ZTEST(i2s_stm32_sai_continuous_rx, test_stop_alias_publishes_only_remaining_tail)
{
	void *borrowed;
	uintptr_t tail_end;
	uint32_t queue_depth;

	fixture_init();
	fixture_drop_and_restart_reversed();
	tail_end = (uintptr_t)fixture_data.stream.continuous_blocks[7] + 16U;
	fixture_publish_prior_tail_blocks_and_request_stop(&borrowed);
	zassert_equal((uintptr_t)fixture_data.stream.continuous_blocks[6], tail_end);
	queue_depth = k_msgq_num_used_get(&fixture_data.stream.queue);
	fixture_complete_dma_stop();
	fixture_callback(DMA_FLAG_TC, tail_end);
	zassert_equal(I2S_STATE_READY, fixture_data.stream.state);
	zassert_equal(queue_depth + 1U, k_msgq_num_used_get(&fixture_data.stream.queue));
	zassert_equal(SAI_CONT_RX_NODE_BORROWED,
		      fixture_data.stream.continuous_node_state[0]);
	zassert_equal(borrowed, fixture_data.stream.continuous_blocks[0]);
	zassert_equal(SAI_CONT_RX_NODE_QUEUED, fixture_data.stream.continuous_node_state[7]);
	zassert_equal(0U, fixture_data.stream.continuous_next_complete);
	fixture_cleanup();
}

ZTEST(i2s_stm32_sai_continuous_rx, test_repeated_terminal_callback_with_all_owners)
{
	void *borrowed;
	uintptr_t tail_end;

	fixture_init();
	fixture_drop_and_restart_reversed();
	tail_end = (uintptr_t)fixture_data.stream.continuous_blocks[7] + 16U;
	fixture_publish_prior_tail_blocks_and_request_stop(&borrowed);
	fixture_complete_dma_stop();
	fixture_callback(DMA_FLAG_HT, tail_end);
	zassert_equal(I2S_STATE_STOPPING, fixture_data.stream.state);
	zassert_true(fixture_data.stream.continuous_stop_pending);
	zassert_equal(7U, k_msgq_num_used_get(&fixture_data.stream.queue));
	zassert_equal(SAI_CONT_RX_NODE_QUEUED, fixture_data.stream.continuous_node_state[7]);
	fixture_callback(DMA_FLAG_TC, tail_end);
	zassert_equal(I2S_STATE_READY, fixture_data.stream.state);
	zassert_equal(7U, k_msgq_num_used_get(&fixture_data.stream.queue));
	zassert_equal(SAI_CONT_RX_NODE_BORROWED,
		      fixture_data.stream.continuous_node_state[0]);
	zassert_equal(borrowed, fixture_data.stream.continuous_blocks[0]);
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

ZTEST(i2s_stm32_sai_continuous_rx, test_reconfigure_restores_normal_dma_and_roundtrips)
{
	struct i2s_config continuous = fixture_continuous_config();
	struct i2s_config normal = fixture_normal_config();
	void *old_first_block;
	void *block;
	size_t size;
	uintptr_t tail_end;

	ARG_UNUSED(sai_sub_dma_init);
	fixture_empty_init();
	zassert_ok(stm32_sai_sub_conf(&fixture_device, I2S_DIR_RX, &continuous));
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	old_first_block = fixture_data.stream.continuous_blocks[0];
	tail_end = (uintptr_t)fixture_data.stream.continuous_blocks[7] +
		   fixture_data.stream.i2s_cfg.block_size;
	fixture_dma_registers.CDAR = (uintptr_t)fixture_data.stream.continuous_blocks[0] + 4U;
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_STOP));
	fixture_complete_dma_stop();
	fixture_callback(DMA_FLAG_TC, tail_end);
	zassert_equal(I2S_STATE_READY, fixture_data.stream.state);
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		zassert_ok(stm32_sai_sub_read(&fixture_device, &block, &size));
		zassert_equal(fixture_data.stream.i2s_cfg.block_size, size);
		zassert_ok(stm32_sai_rx_release(&fixture_device, block));
	}
	zassert_is_null(fixture_data.stream.continuous_blocks[0]);
	zassert_true(fixture_data.stream.continuous_stopped);
	zassert_false(fixture_data.stream.continuous_quiesced);
	zassert_equal(DMA_LINKEDLIST, fixture_data.hdma.Mode);
	zassert_equal(&fixture_data.stream.continuous_queue, fixture_data.hdma.LinkedListQueue);
	zassert_equal((uint32_t)(uintptr_t)old_first_block,
		      fixture_data.stream.continuous_nodes[0]->LinkRegisters[4]);

	zassert_ok(stm32_sai_sub_conf(&fixture_device, I2S_DIR_RX, &normal));
	zassert_false(fixture_data.stream.continuous_rx);

	zassert_equal(1U, fixture_deinit_count);
	zassert_equal(DMA_NORMAL, fixture_data.hdma.Mode);
	zassert_equal(HAL_DMA_STATE_READY, fixture_data.hdma.State);
	zassert_is_null(fixture_data.hdma.LinkedListQueue);
	zassert_equal(&fixture_data.hsai, fixture_data.hdma.Parent);
	zassert_equal(0U, fixture_sai_rx_linked_list_count);
	zassert_equal(0U, fixture_sai_rx_stale_queue_start_count);
	zassert_equal(0U, fixture_normal_start_count);
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	zassert_equal(HAL_DMA_STATE_BUSY, fixture_data.hdma.State);
	zassert_equal(1U, fixture_normal_start_count);
	zassert_equal(fixture_data.stream.mem_block, fixture_normal_start_destination);
	zassert_equal((uint32_t)(uintptr_t)old_first_block,
		      fixture_data.stream.continuous_nodes[0]->LinkRegisters[4]);
	zassert_ok(HAL_SAI_DMAStop(&fixture_data.hsai));
	k_mem_slab_free(&fixture_slab, fixture_data.stream.mem_block);
	fixture_data.stream.mem_block = NULL;
	fixture_data.stream.mem_block_len = 0U;
	fixture_data.stream.state = I2S_STATE_READY;
	zassert_ok(stm32_sai_sub_conf(&fixture_device, I2S_DIR_RX, &continuous));
	zassert_true(fixture_data.stream.continuous_rx);
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	zassert_equal(DMA_LINKEDLIST, fixture_data.hdma.Mode);
	zassert_equal(&fixture_data.stream.continuous_queue, fixture_data.hdma.LinkedListQueue);
	zassert_equal(&fixture_data.hsai, fixture_data.hdma.Parent);
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_DROP));
	fixture_cleanup();
}

ZTEST(i2s_stm32_sai_continuous_rx, test_failed_continuous_start_can_reconfigure_normal)
{
	struct i2s_config continuous = fixture_continuous_config();
	struct i2s_config normal = fixture_normal_config();

	fixture_empty_init();
	zassert_ok(stm32_sai_sub_conf(&fixture_device, I2S_DIR_RX, &continuous));
	fixture_list_start_fails = true;
	zassert_equal(-EIO,
		      stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	zassert_equal(I2S_STATE_READY, fixture_data.stream.state);
	zassert_true(fixture_data.stream.continuous_quiesced);
	zassert_equal(DMA_NORMAL, fixture_data.hdma.Mode);
	zassert_equal(HAL_DMA_STATE_RESET, fixture_data.hdma.State);
	zassert_is_null(fixture_data.hdma.Parent);
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		zassert_is_null(fixture_data.stream.continuous_blocks[i]);
		zassert_equal(SAI_CONT_RX_NODE_FREE,
			      fixture_data.stream.continuous_node_state[i]);
	}

	fixture_list_start_fails = false;
	zassert_ok(stm32_sai_sub_conf(&fixture_device, I2S_DIR_RX, &normal));
	zassert_equal(HAL_DMA_STATE_READY, fixture_data.hdma.State);
	zassert_equal(&fixture_data.hsai, fixture_data.hdma.Parent);
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	zassert_equal(1U, fixture_normal_start_count);
	zassert_equal(HAL_DMA_STATE_BUSY, fixture_data.hdma.State);
	fixture_cleanup();
}

ZTEST(i2s_stm32_sai_continuous_rx, test_list_init_timeout_requires_drop_before_reconfigure)
{
	struct i2s_config continuous = fixture_continuous_config();
	struct i2s_config normal = fixture_normal_config();

	fixture_empty_init();
	zassert_ok(stm32_sai_sub_conf(&fixture_device, I2S_DIR_RX, &continuous));
	fixture_list_init_fails = true;
	zassert_equal(-EIO,
		      stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	zassert_equal(I2S_STATE_ERROR, fixture_data.stream.state);
	zassert_true(fixture_data.stream.continuous_fault);
	zassert_false(fixture_data.stream.continuous_quiesced);
	zassert_true((fixture_dma_registers.CCR & DMA_CCR_EN) != 0U);
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		zassert_is_null(fixture_data.stream.continuous_blocks[i]);
	}

	fixture_list_init_fails = false;
	zassert_equal(-EIO,
		      stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_DROP));
	zassert_equal(I2S_STATE_ERROR, fixture_data.stream.state);
	zassert_false(fixture_data.stream.continuous_quiesced);
	zassert_true((fixture_dma_registers.CCR & DMA_CCR_EN) != 0U);
	zassert_equal(0U, fixture_deinit_count);
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		zassert_is_null(fixture_data.stream.continuous_blocks[i]);
		zassert_equal(SAI_CONT_RX_NODE_FREE,
			      fixture_data.stream.continuous_node_state[i]);
	}

	/* HAL_SAI_DMAStop cannot abort DMA before SAI enters BUSY_RX. */
	fixture_complete_dma_stop();
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_DROP));
	zassert_equal(I2S_STATE_READY, fixture_data.stream.state);
	zassert_true(fixture_data.stream.continuous_quiesced);
	zassert_false((fixture_dma_registers.CCR & DMA_CCR_EN) != 0U);
	zassert_equal(1U, fixture_deinit_count);
	zassert_ok(stm32_sai_sub_conf(&fixture_device, I2S_DIR_RX, &normal));
	zassert_equal(HAL_DMA_STATE_READY, fixture_data.hdma.State);
	zassert_equal(&fixture_data.hsai, fixture_data.hdma.Parent);
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	zassert_equal(1U, fixture_normal_start_count);
	fixture_cleanup();
}

ZTEST(i2s_stm32_sai_continuous_rx, test_start_rollback_deinit_failure_preserves_blocks)
{
	struct i2s_config continuous = fixture_continuous_config();
	struct i2s_config normal = fixture_normal_config();

	fixture_empty_init();
	zassert_ok(stm32_sai_sub_conf(&fixture_device, I2S_DIR_RX, &continuous));
	fixture_list_start_fails = true;
	fixture_deinit_fails = true;
	zassert_equal(-EIO,
		      stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	zassert_equal(I2S_STATE_ERROR, fixture_data.stream.state);
	zassert_true(fixture_data.stream.continuous_fault);
	zassert_false(fixture_data.stream.continuous_quiesced);
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		zassert_not_null(fixture_data.stream.continuous_blocks[i]);
		zassert_equal(SAI_CONT_RX_NODE_DMA,
			      fixture_data.stream.continuous_node_state[i]);
	}

	fixture_list_start_fails = false;
	fixture_deinit_fails = false;
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_PREPARE));
	zassert_equal(I2S_STATE_READY, fixture_data.stream.state);
	zassert_true(fixture_data.stream.continuous_quiesced);
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		zassert_is_null(fixture_data.stream.continuous_blocks[i]);
		zassert_equal(SAI_CONT_RX_NODE_FREE,
			      fixture_data.stream.continuous_node_state[i]);
	}
	zassert_ok(stm32_sai_sub_conf(&fixture_device, I2S_DIR_RX, &normal));
	zassert_equal(HAL_DMA_STATE_READY, fixture_data.hdma.State);
	zassert_equal(&fixture_data.hsai, fixture_data.hdma.Parent);
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	zassert_equal(1U, fixture_normal_start_count);
	fixture_cleanup();
}

ZTEST(i2s_stm32_sai_continuous_rx, test_start_error_after_enable_keeps_dma_owners)
{
	struct i2s_config continuous = fixture_continuous_config();
	struct i2s_config normal = fixture_normal_config();

	fixture_empty_init();
	zassert_ok(stm32_sai_sub_conf(&fixture_device, I2S_DIR_RX, &continuous));
	fixture_list_start_fails_after_enable = true;
	zassert_equal(-EIO,
		      stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	zassert_equal(I2S_STATE_ERROR, fixture_data.stream.state);
	zassert_true(fixture_data.stream.continuous_fault);
	zassert_false(fixture_data.stream.continuous_quiesced);
	zassert_equal(DMA_LINKEDLIST, fixture_data.hdma.Mode);
	zassert_equal(HAL_DMA_STATE_BUSY, fixture_data.hdma.State);
	zassert_true((fixture_dma_registers.CCR & DMA_CCR_EN) != 0U);
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		zassert_not_null(fixture_data.stream.continuous_blocks[i]);
		zassert_equal(SAI_CONT_RX_NODE_DMA,
			      fixture_data.stream.continuous_node_state[i]);
	}

	/* Fault injection covers the driver's conservative EN-set branch. */
	fixture_list_start_fails_after_enable = false;
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_PREPARE));
	zassert_equal(I2S_STATE_READY, fixture_data.stream.state);
	zassert_true(fixture_data.stream.continuous_quiesced);
	zassert_false((fixture_dma_registers.CCR & DMA_CCR_EN) != 0U);
	zassert_ok(stm32_sai_sub_conf(&fixture_device, I2S_DIR_RX, &normal));
	zassert_equal(HAL_DMA_STATE_READY, fixture_data.hdma.State);
	zassert_equal(&fixture_data.hsai, fixture_data.hdma.Parent);
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	zassert_equal(1U, fixture_normal_start_count);
	fixture_cleanup();
}

ZTEST(i2s_stm32_sai_continuous_rx, test_first_dma_fault_survives_cleanup_and_restart_gate)
{
	struct stm32_sai_rx_evidence before;
	struct stm32_sai_rx_evidence after;
	void *borrowed;
	size_t size;

	fixture_init();
	fixture_callback(DMA_FLAG_HT, (uintptr_t)fixture_data.stream.continuous_blocks[1] + 4U);
	zassert_ok(stm32_sai_sub_read(&fixture_device, &borrowed, &size));
	fixture_data.hsai.ErrorCode = 0x1234U;
	fixture_data.hdma.ErrorCode = 0x5678U;
	fixture_dma_registers.CBR1 = 12U;
	fixture_dma_registers.CSAR = 0x46005420U;
	fixture_dma_registers.CLLR = 0x3fU;
	fixture_sai_registers.SR = SAI_xSR_OVRUDR;
	fixture_callback(DMA_FLAG_TC | DMA_FLAG_DTE,
			 (uintptr_t)fixture_data.stream.continuous_blocks[1] + 8U);
	zassert_ok(stm32_sai_rx_get_evidence(&fixture_device, &before));
	zassert_equal(STM32_SAI_RX_FAULT_DMA, before.first_fault.reason);
	zassert_equal(I2S_STATE_RUNNING, before.first_fault.stream_state);
	zassert_equal(DMA_FLAG_TC | DMA_FLAG_DTE, before.first_fault.dma_flags);
	zassert_equal(DMA_CCR_EN, before.first_fault.dma_ccr);
	zassert_equal(SAI_xCR1_DMAEN | SAI_xCR1_SAIEN, before.first_fault.sai_cr1);
	zassert_equal(SAI_xSR_OVRUDR, before.first_fault.sai_sr);
	zassert_equal(0x1234U, before.first_fault.sai_error);
	zassert_equal(0x5678U, before.first_fault.dma_error);
	zassert_equal(12U, before.first_fault.dma_cbr1);
	zassert_equal(0x46005420U, before.first_fault.dma_csar);
	zassert_equal(0x3fU, before.first_fault.dma_cllr);
	zassert_equal(1U, before.first_fault.completion_count);
	zassert_equal(SAI_CONT_RX_NODE_BORROWED, before.first_fault.node_states & 3U);
	zassert_equal(0U, fixture_data.hdma.flags);

	/* Later interrupts, HAL cleanup and rejected starts must retain the packet. */
	fixture_callback(DMA_FLAG_ULE, 0U);
	fixture_data.hsai.ErrorCode = 0U;
	fixture_data.hdma.ErrorCode = 0U;
	fixture_sai_registers.SR = 0U;
	fixture_complete_dma_stop();
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_PREPARE));
	zassert_equal(-EBUSY,
		      stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	zassert_ok(stm32_sai_rx_get_evidence(&fixture_device, &after));
	zassert_mem_equal(&before.first_fault, &after.first_fault, sizeof(before.first_fault));
	zassert_ok(stm32_sai_rx_release(&fixture_device, borrowed));
	zassert_ok(stm32_sai_sub_trigger(&fixture_device, I2S_DIR_RX, I2S_TRIGGER_START));
	zassert_ok(stm32_sai_rx_get_evidence(&fixture_device, &after));
	zassert_equal(STM32_SAI_RX_FAULT_NONE, after.first_fault.reason);
	fixture_cleanup();
}

ZTEST(i2s_stm32_sai_continuous_rx, test_fault_flags_and_negative_callback_status)
{
	const uint32_t flags[] = {DMA_FLAG_DTE, DMA_FLAG_ULE,  DMA_FLAG_USE,
				  DMA_FLAG_TO,  DMA_FLAG_SUSP, 0U};

	for (size_t i = 0U; i < ARRAY_SIZE(flags); i++) {
		struct stm32_sai_rx_evidence evidence;

		fixture_init();
		fixture_data.hdma.flags = flags[i];
		int status = flags[i] == 0U ? -EIO : 0;

		dma_callback(NULL, &fixture_data.hdma, 0U, status);
		zassert_ok(stm32_sai_rx_get_evidence(&fixture_device, &evidence));
		zassert_equal(STM32_SAI_RX_FAULT_DMA, evidence.first_fault.reason);
		zassert_equal(status, evidence.first_fault.status);
		zassert_equal(flags[i], evidence.first_fault.dma_flags);
		fixture_cleanup();
	}
}

ZTEST(i2s_stm32_sai_continuous_rx, test_prefix_fault_retains_acknowledged_callback_flags)
{
	struct stm32_sai_rx_evidence evidence;

	fixture_init();
	fixture_callback(DMA_FLAG_TC, 0U);
	zassert_ok(stm32_sai_rx_get_evidence(&fixture_device, &evidence));
	zassert_equal(STM32_SAI_RX_FAULT_PREFIX, evidence.first_fault.reason);
	zassert_equal(DMA_FLAG_TC, evidence.first_fault.dma_flags);
	zassert_equal(DMA_CCR_EN, evidence.first_fault.dma_ccr);
	fixture_cleanup();
}

ZTEST(i2s_stm32_sai_continuous_rx, test_queue_fault_has_distinct_reason)
{
	struct stm32_sai_rx_evidence evidence;
	struct queue_item item = {0};

	fixture_init();
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		zassert_ok(k_msgq_put(&fixture_data.stream.queue, &item, K_NO_WAIT));
	}
	fixture_callback(DMA_FLAG_HT, (uintptr_t)fixture_data.stream.continuous_blocks[1] + 4U);
	zassert_ok(stm32_sai_rx_get_evidence(&fixture_device, &evidence));
	zassert_equal(STM32_SAI_RX_FAULT_QUEUE, evidence.first_fault.reason);
	zassert_equal(DMA_FLAG_HT, evidence.first_fault.dma_flags);
	fixture_cleanup();
}

ZTEST(i2s_stm32_sai_continuous_rx, test_polled_sai_fault_is_retained_before_disable)
{
	struct stm32_sai_rx_evidence evidence;
	void *block;
	size_t size;

	fixture_init();
	fixture_sai_registers.SR = SAI_xSR_LFSDET;
	zassert_equal(-EIO, stm32_sai_sub_read(&fixture_device, &block, &size));
	zassert_ok(stm32_sai_rx_get_evidence(&fixture_device, &evidence));
	zassert_equal(STM32_SAI_RX_FAULT_READ_SAI, evidence.first_fault.reason);
	zassert_equal(SAI_xSR_LFSDET, evidence.first_fault.sai_sr);
	zassert_equal(DMA_CCR_EN, evidence.first_fault.dma_ccr);
	fixture_cleanup();
}

ZTEST(i2s_stm32_sai_continuous_rx, test_release_idle_fault_and_healthy_callback)
{
	struct stm32_sai_rx_evidence evidence;
	void *block;
	size_t size;

	fixture_init();
	fixture_callback(DMA_FLAG_HT, (uintptr_t)fixture_data.stream.continuous_blocks[1] + 4U);
	zassert_ok(stm32_sai_rx_get_evidence(&fixture_device, &evidence));
	zassert_equal(STM32_SAI_RX_FAULT_NONE, evidence.first_fault.reason);
	zassert_ok(stm32_sai_sub_read(&fixture_device, &block, &size));
	fixture_complete_dma_stop();
	zassert_equal(-EIO, stm32_sai_rx_release(&fixture_device, block));
	zassert_ok(stm32_sai_rx_get_evidence(&fixture_device, &evidence));
	zassert_equal(STM32_SAI_RX_FAULT_RELEASE_IDLE, evidence.first_fault.reason);
	zassert_equal(0U, evidence.first_fault.dma_ccr & DMA_CCR_EN);
	zassert_equal(1U, evidence.first_fault.append_count);
	fixture_cleanup();
}

#endif /* I2S_STM32_SAI_DRIVER_FIXTURE_TESTS_H_ */
