/*
 * Copyright (c) 2026 Hula Firmware
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include "i2s_stm32_sai_rx_ring.h"

#define NODE_COUNT 8U
#define NODE_FREE 0U
#define NODE_DMA 1U
#define NODE_BORROWED 3U

ZTEST(i2s_stm32_sai_continuous_rx, test_quiesced_release_advances_fifo)
{
	uint8_t states[NODE_COUNT] = {NODE_BORROWED, NODE_BORROWED};
	uint8_t next_release = 0U;
	uint8_t block0;
	uint8_t block1;
	void *blocks[NODE_COUNT] = {&block0, &block1};

	zassert_false(sai_rx_release_quiesced(1U, NODE_COUNT, &next_release, states,
					     NODE_BORROWED, NODE_FREE, blocks, &block1));
	zassert_equal(0U, next_release);
	zassert_equal(NODE_BORROWED, states[0]);
	zassert_equal(&block0, blocks[0]);
	zassert_true(sai_rx_release_quiesced(0U, NODE_COUNT, &next_release, states,
					    NODE_BORROWED, NODE_FREE, blocks, &block0));
	zassert_equal(1U, next_release);
	zassert_equal(NODE_FREE, states[0]);
	zassert_is_null(blocks[0]);
	zassert_equal(NODE_BORROWED, states[1]);
	zassert_equal(&block1, blocks[1]);
	zassert_true(sai_rx_release_quiesced(1U, NODE_COUNT, &next_release, states,
					    NODE_BORROWED, NODE_FREE, blocks, &block1));
	zassert_equal(2U, next_release);
	zassert_equal(NODE_FREE, states[1]);
	zassert_is_null(blocks[1]);
}

ZTEST(i2s_stm32_sai_continuous_rx, test_completion_prefix_is_ordered_and_wraps)
{
	uint8_t states[NODE_COUNT] = {NODE_DMA, NODE_DMA, NODE_DMA, NODE_DMA,
				      NODE_DMA, NODE_DMA, NODE_DMA, NODE_DMA};
	uint8_t publish_count = 0U;

	zassert_equal(SAI_RX_PREFIX_READY,
		      sai_rx_plan_completed_prefix(6U, 2, NODE_COUNT, states, NODE_DMA,
					   false, true, false, &publish_count));
	zassert_equal(4U, publish_count);
	states[7] = NODE_BORROWED;
	zassert_equal(SAI_RX_PREFIX_FAULT,
		      sai_rx_plan_completed_prefix(6U, 2, NODE_COUNT, states, NODE_DMA,
					   false, true, false, &publish_count));
	zassert_equal(0U, publish_count);
}

ZTEST(i2s_stm32_sai_continuous_rx, test_stop_prefix_includes_terminal_tail)
{
	uint8_t states[NODE_COUNT] = {NODE_DMA, NODE_DMA, NODE_DMA, NODE_DMA,
				      NODE_DMA, NODE_DMA, NODE_DMA, NODE_DMA};
	uint8_t publish_count = 0U;

	zassert_equal(SAI_RX_PREFIX_READY,
		      sai_rx_plan_prefix_through(6U, 1U, NODE_COUNT, states, NODE_DMA,
						 &publish_count));
	zassert_equal(4U, publish_count);
	zassert_true(sai_rx_prefix_contains(6U, publish_count, NODE_COUNT, 7U));
	zassert_true(sai_rx_prefix_contains(6U, publish_count, NODE_COUNT, 0U));
	zassert_true(sai_rx_prefix_contains(6U, publish_count, NODE_COUNT, 1U));
	zassert_false(sai_rx_prefix_contains(6U, publish_count, NODE_COUNT, 2U));

	states[0] = NODE_BORROWED;
	zassert_equal(SAI_RX_PREFIX_FAULT,
		      sai_rx_plan_prefix_through(6U, 1U, NODE_COUNT, states, NODE_DMA,
						 &publish_count));
}

ZTEST_SUITE(i2s_stm32_sai_continuous_rx, NULL, NULL, NULL, NULL, NULL);
