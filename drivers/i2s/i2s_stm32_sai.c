/*
 * Copyright (c) 2024-2025 ZAL Zentrum für Angewandte Luftfahrtforschung GmbH
 * Copyright (c) 2024-2026 Mario Paja
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT st_stm32_sai

#include <string.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/dma/dma_stm32.h>
#include <soc.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/cache.h>

#include <stm32_ll_dma.h>
#include <stm32_bitops.h>
#if defined(CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6)
#include <stm32n6xx_hal_dma_ex.h>
#include <zephyr/drivers/i2s/stm32_sai.h>
#include "i2s_stm32_sai_rx_ring.h"
#endif

#include <zephyr/logging/log.h>
#include <zephyr/irq.h>
LOG_MODULE_REGISTER(i2s_stm32_sai, CONFIG_I2S_LOG_LEVEL);

enum mclk_divider {
	MCLK_NO_DIV,
	MCLK_DIV_256,
	MCLK_DIV_512
};

static const uint32_t sai_fifo_threshold[] = {
	SAI_FIFOTHRESHOLD_EMPTY,
	SAI_FIFOTHRESHOLD_1QF,
	SAI_FIFOTHRESHOLD_HF,
	SAI_FIFOTHRESHOLD_3QF,
	SAI_FIFOTHRESHOLD_FULL,
};

struct queue_item {
	void *buffer;
	size_t size;
};

#if defined(CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6)
#define SAI_CONT_RX_NODE_COUNT 8U
#define SAI_CONT_RX_NODE_FREE 0U
#define SAI_CONT_RX_NODE_DMA 1U
#define SAI_CONT_RX_NODE_QUEUED 2U
#define SAI_CONT_RX_NODE_BORROWED 3U
#define SAI_CONT_RX_LINK_UPDATE_MASK \
	(DMA_CLLR_UT1 | DMA_CLLR_UT2 | DMA_CLLR_UB1 | DMA_CLLR_USA | DMA_CLLR_UDA | DMA_CLLR_ULL)

struct sai_continuous_node_storage {
	DMA_NodeTypeDef node;
	uint32_t padding[7];
} __aligned(32);

BUILD_ASSERT(sizeof(struct sai_continuous_node_storage) == 64U);
BUILD_ASSERT(sizeof(struct sai_continuous_node_storage) * SAI_CONT_RX_NODE_COUNT == 512U);
BUILD_ASSERT(CONFIG_I2S_STM32_SAI_BLOCK_COUNT >= SAI_CONT_RX_NODE_COUNT);
#define SAI_CONT_NODE_STORAGE_DECL(node) \
	static struct sai_continuous_node_storage sai_node_storage_##node[SAI_CONT_RX_NODE_COUNT] \
		__nocache __aligned(512);
#define SAI_CONT_NODE_STORAGE_INIT(node) .continuous_node_storage = sai_node_storage_##node,
#else
#define SAI_CONT_NODE_STORAGE_DECL(node)
#define SAI_CONT_NODE_STORAGE_INIT(node)
#endif

struct stream {
	DMA_TypeDef *reg;

	const struct device *dma_dev;
	uint32_t dma_channel;
	struct dma_config dma_cfg;

	/* DMA size of a source block transfer in bytes according SAI data size. */
	uint8_t dma_src_size;

	struct i2s_config i2s_cfg;
	void *mem_block;
	size_t mem_block_len;

	bool master;
	bool last_block;

	int32_t state;
	struct k_msgq queue;

	int (*stream_start)(const struct device *dev, enum i2s_dir dir);
	void (*queue_drop)(const struct device *dev);
#if defined(CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6)
	bool continuous_rx;
	bool continuous_quiesced;
	bool continuous_stop_pending;
	bool continuous_stopped;
	bool continuous_stop_target_complete;
	uint8_t continuous_stop_target;
	bool continuous_fault;
	DMA_NodeTypeDef *continuous_nodes[SAI_CONT_RX_NODE_COUNT];
	DMA_NodeTypeDef *continuous_tail;
	uint32_t continuous_link_mask;
	DMA_QListTypeDef continuous_queue;
	void *continuous_blocks[SAI_CONT_RX_NODE_COUNT];
	uint8_t continuous_node_state[SAI_CONT_RX_NODE_COUNT];
	uint8_t continuous_next_complete;
	uint8_t continuous_next_release;
	atomic_t continuous_queue_high_water;
	uint32_t continuous_slab_high_water;
#endif
};

struct stm32_sai_sub_data {
	SAI_HandleTypeDef hsai;
	DMA_HandleTypeDef hdma;
	struct stream stream;
	struct queue_item queue_buffer[CONFIG_I2S_STM32_SAI_BLOCK_COUNT];
#if defined(CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6)
	struct sai_continuous_node_storage *continuous_node_storage;
#endif
};

struct stm32_sai_sub_cfg {
	const struct pinctrl_dev_config *pincfg;
	bool mclk_enable;
	enum mclk_divider mclk_div;
	bool synchronous;
	enum i2s_dir dir;

	const struct device *controller;
};

struct stm32_sai_cfg {
	const struct stm32_pclken sai_ck;
	const struct stm32_pclken sai_ker_ck;
	const struct stm32_pclken sai_b_ker_ck; /* Dedicated to SAIn_B, if applicable */
	bool has_sai_ker_ck: 1;
	bool has_sai_b_ker_ck: 1;
};

static inline void sai_sub_disable(SAI_HandleTypeDef *hsai, i2s_opt_t options)
{
	if ((options & I2S_OPT_BIT_CLK_GATED) == 0) {
		LOG_DBG("SAI sub-block %p not disabled: bit clock gating disabled", hsai->Instance);
		return;
	}

	if (hsai->Init.Synchro == SAI_SYNCHRONOUS) {
		LOG_DBG("SAI sub-block %p not disabled: configured as synchronous peripheral",
			hsai->Instance);
		return;
	}

	__HAL_SAI_DISABLE(hsai);

	LOG_DBG("SAI Disabled");
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
	struct stm32_sai_sub_data *sub_data = CONTAINER_OF(hsai, struct stm32_sai_sub_data, hsai);
	struct stream *stream = &sub_data->stream;
	int ret;

	/* Exit the callback, Stream is stopped */
	if (stream->state == I2S_STATE_ERROR) {
		goto exit;
	}

	if (stream->mem_block == NULL) {
		if (stream->state != I2S_STATE_READY) {
			stream->state = I2S_STATE_ERROR;
			LOG_ERR("RX mem_block NULL");
			sai_sub_disable(hsai, stream->i2s_cfg.options);
			goto exit;
		} else {
			return;
		}
	}

	struct queue_item item = {.buffer = stream->mem_block, .size = stream->mem_block_len};

	ret = k_msgq_put(&stream->queue, &item, K_NO_WAIT);
	if (ret < 0) {
		stream->state = I2S_STATE_ERROR;
		sai_sub_disable(hsai, stream->i2s_cfg.options);
		goto exit;
	}

	if (stream->state == I2S_STATE_STOPPING) {
		stream->state = I2S_STATE_READY;
		LOG_DBG("Stopping RX ...");
		sai_sub_disable(hsai, stream->i2s_cfg.options);
		goto exit;
	}

	ret = k_mem_slab_alloc(stream->i2s_cfg.mem_slab, &stream->mem_block, K_NO_WAIT);
	if (ret < 0) {
		stream->state = I2S_STATE_ERROR;
		sai_sub_disable(hsai, stream->i2s_cfg.options);
		goto exit;
	}

	stream->mem_block_len = stream->i2s_cfg.block_size;

	if (HAL_SAI_Receive_DMA(hsai, stream->mem_block,
				stream->mem_block_len / stream->dma_src_size) != HAL_OK) {
		LOG_ERR("HAL_SAI_Receive_DMA: <FAILED>");
	}

exit:
	/* EXIT */
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
	struct stm32_sai_sub_data *sub_data = CONTAINER_OF(hsai, struct stm32_sai_sub_data, hsai);
	struct stream *stream = &sub_data->stream;
	void *mem_block_tmp = stream->mem_block;
	struct queue_item item;
	int ret;

	if (stream->state == I2S_STATE_ERROR) {
		LOG_ERR("TX bad status: %d, Stopping...", stream->state);
		sai_sub_disable(hsai, stream->i2s_cfg.options);
		goto exit;
	}

	if (stream->mem_block == NULL) {
		if (stream->state != I2S_STATE_READY) {
			stream->state = I2S_STATE_ERROR;
			LOG_ERR("TX mem_block NULL");
			sai_sub_disable(hsai, stream->i2s_cfg.options);
			goto exit;
		} else {
			return;
		}
	}

	if (stream->last_block) {
		LOG_DBG("TX Stopped ...");
		stream->state = I2S_STATE_READY;
		stream->mem_block = NULL;
		sai_sub_disable(hsai, stream->i2s_cfg.options);
		goto exit;
	}

	/* Exit callback, no more data in the queue */
	/* Reset I2S state */
	if (k_msgq_num_used_get(&stream->queue) == 0) {
		LOG_DBG("Exit TX callback, no more data in the queue");
		stream->state = I2S_STATE_READY;
		stream->mem_block = NULL;
		sai_sub_disable(hsai, stream->i2s_cfg.options);
		goto exit;
	}

	ret = k_msgq_get(&stream->queue, &item, K_NO_WAIT);
	if (ret < 0) {
		stream->state = I2S_STATE_ERROR;
		sai_sub_disable(hsai, stream->i2s_cfg.options);
		goto exit;
	}

	stream->mem_block = item.buffer;
	stream->mem_block_len = item.size;

	sys_cache_data_flush_range(stream->mem_block, stream->mem_block_len);

	if (HAL_SAI_Transmit_DMA(hsai, stream->mem_block,
				 stream->mem_block_len / stream->dma_src_size) != HAL_OK) {
		LOG_ERR("HAL_SAI_Transmit_DMA: <FAILED>");
	}

exit:
	/* Free memory slab & exit */
	k_mem_slab_free(stream->i2s_cfg.mem_slab, mem_block_tmp);
}

void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai)
{
	switch (HAL_SAI_GetError(hsai)) {
	case HAL_SAI_ERROR_NONE:
		LOG_INF("No error");
		break;
	case HAL_SAI_ERROR_OVR:
		LOG_WRN("Overrun Error");
		break;
	case HAL_SAI_ERROR_UDR:
		LOG_WRN("Underrun Error");
		break;
	case HAL_SAI_ERROR_AFSDET:
		LOG_WRN("Anticipated Frame synchronisation detection");
		break;
	case HAL_SAI_ERROR_LFSDET:
		LOG_WRN("Late Frame synchronisation detection");
		break;
	case HAL_SAI_ERROR_CNREADY:
		LOG_WRN("codec not ready");
		break;
	case HAL_SAI_ERROR_TIMEOUT:
		LOG_WRN("Timeout error");
		break;
	case HAL_SAI_ERROR_DMA:
		LOG_WRN("DMA error");
		break;
	default:
		LOG_ERR("Unknown error");
	}
}

static int stm32_sai_clock_en(const struct device *dev)
{
	const struct stm32_sai_cfg *sai_cfg = dev->config;
	const struct device *clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
	int ret;

	/* Turn on SAI peripheral clock */
	ret = clock_control_on(clk, (clock_control_subsys_t)&sai_cfg->sai_ck);
	if (ret != 0) {
		return -EIO;
	}

	/* Configure shared SAIn_A & SAIn_B or dedicated SAIn_A kernel clock */
	if (sai_cfg->has_sai_ker_ck) {
		ret = clock_control_configure(clk, (clock_control_subsys_t)&sai_cfg->sai_ker_ck,
					      NULL);
		if (ret != 0) {
			return -EIO;
		}
	}

	/* Configure dedicated SAI B kernel clock */
	if (sai_cfg->has_sai_b_ker_ck) {
		ret = clock_control_configure(clk, (clock_control_subsys_t)&sai_cfg->sai_b_ker_ck,
					      NULL);
		if (ret != 0) {
			return -EIO;
		}
	}

	return 0;
}

static int sai_sub_dma_init(const struct device *dev)
{
	struct stm32_sai_sub_data *sub_data = dev->data;
	struct stream *stream = &sub_data->stream;
	struct dma_config *dma_cfg = &sub_data->stream.dma_cfg;
	uint16_t source_addr_adj, dest_addr_adj;
	int ret;

	SAI_HandleTypeDef *hsai = &sub_data->hsai;
	DMA_HandleTypeDef *hdma = &sub_data->hdma;

	if (!device_is_ready(stream->dma_dev)) {
		LOG_ERR("%s DMA device not ready", stream->dma_dev->name);
		return -ENODEV;
	}

	/* Proceed to the minimum Zephyr DMA driver init */
	dma_cfg->user_data = hdma;
	if (dma_cfg->channel_direction == (enum dma_channel_direction)MEMORY_TO_PERIPHERAL) {
		source_addr_adj = DMA_ADDR_ADJ_INCREMENT;
		dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
	} else {
		source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
		dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;
	}

	/* HACK: This field is used to inform driver that it is overridden */
	dma_cfg->linked_channel = STM32_DMA_HAL_OVERRIDE;

	ret = dma_config(stream->dma_dev, stream->dma_channel, dma_cfg);
	if (ret != 0) {
		LOG_ERR("Failed to configure DMA channel %d", stream->dma_channel);
		return ret;
	}

	ret = dma_stm32_zcfg_to_halcfg(stream->dma_dev, dma_cfg, &hdma->Init,
				       source_addr_adj, dest_addr_adj);
	if (ret < 0) {
		return ret;
	}

#if defined(CONFIG_DMA_STM32U5)
	hdma->Init.SrcBurstLength = 1;
	hdma->Init.DestBurstLength = 1;
	hdma->Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
#endif

	hdma->Instance = STM32_DMA_GET_INSTANCE(stream->reg, stream->dma_channel);

	if (dma_cfg->channel_direction == (enum dma_channel_direction)MEMORY_TO_PERIPHERAL) {
		__HAL_LINKDMA(hsai, hdmatx, sub_data->hdma);
	} else {
		__HAL_LINKDMA(hsai, hdmarx, sub_data->hdma);
	}

	if (HAL_DMA_Init(&sub_data->hdma) != HAL_OK) {
		LOG_ERR("HAL_DMA_Init: <FAILED>");
		return -EIO;
	}

#if defined(CONFIG_SOC_SERIES_STM32N6X)
	if (HAL_DMA_ConfigChannelAttributes(&sub_data->hdma, DMA_CHANNEL_SEC | DMA_CHANNEL_PRIV |
					    DMA_CHANNEL_SRC_SEC | DMA_CHANNEL_DEST_SEC) != HAL_OK) {
		LOG_ERR("HAL_DMA_ConfigChannelAttributes: <Failed>");
		return -EIO;
	}
#elif defined(CONFIG_DMA_STM32U5)
	if (HAL_DMA_ConfigChannelAttributes(&sub_data->hdma, DMA_CHANNEL_NPRIV) != HAL_OK) {
		LOG_ERR("HAL_DMA_ConfigChannelAttributes: <Failed>");
		return -EIO;
	}
#endif

	LOG_DBG("dma@%08x Init: <OK>", (uint32_t)hdma->Instance);

	return 0;
}

static int sai_sub_init(const struct device *dev)
{
	struct stm32_sai_sub_data *sub_data = dev->data;
	const struct stm32_sai_sub_cfg *sub_cfg = dev->config;
	struct stream *stream = &sub_data->stream;
	int ret = 0;
#if defined(CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6)
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		stream->continuous_nodes[i] = &sub_data->continuous_node_storage[i].node;
	}
#endif

	/* Configure DT provided pins */
	ret = pinctrl_apply_state(sub_cfg->pincfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("SAI Sub-Block pinctrl setup: <FAILED>, ret: %d", ret);
		return ret;
	}

	if (!device_is_ready(sub_data->stream.dma_dev)) {
		LOG_ERR("%s device not ready", sub_data->stream.dma_dev->name);
		return -ENODEV;
	}

	k_msgq_init(&stream->queue, (char *)sub_data->queue_buffer, sizeof(struct queue_item),
		    ARRAY_SIZE(sub_data->queue_buffer));

	/* Initialize DMA */
	ret = sai_sub_dma_init(dev);
	if (ret < 0) {
		LOG_ERR("SAI Sub-Block DMA Init <FAILED>, ret: %d", ret);
		return ret;
	}

	/* State set to not ready until successfully configured */
	stream->state = I2S_STATE_NOT_READY;

	return 0;
}

static void dma_callback(const struct device *dma_dev, void *arg, uint32_t channel, int status)
{
	DMA_HandleTypeDef *hdma = arg;

	ARG_UNUSED(dma_dev);
#if defined(CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6)
	struct stm32_sai_sub_data *sub_data = CONTAINER_OF(hdma, struct stm32_sai_sub_data, hdma);
	struct stream *stream = &sub_data->stream;
	if (stream->continuous_rx) {
		uint32_t flags = __HAL_DMA_GET_FLAG(hdma, DMA_FLAG_TC | DMA_FLAG_HT | DMA_FLAG_DTE |
						 DMA_FLAG_ULE | DMA_FLAG_USE | DMA_FLAG_TO | DMA_FLAG_SUSP);
		if (stream->state != I2S_STATE_RUNNING &&
		    !(stream->state == I2S_STATE_STOPPING && stream->continuous_stop_pending)) {
			if (flags != 0U) {
				__HAL_DMA_CLEAR_FLAG(hdma, flags);
			}
			return;
		}
		uint32_t sai_errors = sub_data->hsai.Instance->SR &
			(SAI_xSR_OVRUDR | SAI_xSR_WCKCFG | SAI_xSR_AFSDET | SAI_xSR_LFSDET);
		uint32_t errors = flags & (DMA_FLAG_DTE | DMA_FLAG_ULE | DMA_FLAG_USE |
					   DMA_FLAG_TO | DMA_FLAG_SUSP);
		uint32_t tc = flags & DMA_FLAG_TC;
		uint32_t ht = flags & DMA_FLAG_HT;
		if (status < 0 || errors != 0U || sai_errors != 0U) {
			if (flags != 0U) {
				__HAL_DMA_CLEAR_FLAG(hdma, flags);
			}
			stream->continuous_fault = true;
			stream->state = I2S_STATE_ERROR;
			__HAL_DMA_DISABLE(hdma);
			sub_data->hsai.Instance->CR1 &= ~(SAI_xCR1_DMAEN | SAI_xCR1_SAIEN);
			return;
		}
		if (tc == 0U && ht == 0U) {
			return;
		}
		__HAL_DMA_CLEAR_FLAG(hdma, tc | ht);
		uintptr_t current = hdma->Instance->CDAR;
		int active_index = -1;
		bool ambiguous_boundary = false;
		bool terminal_end = false;
		bool tail_end = false;
		int terminal_index = -1;
		int boundary_end_index = -1;
		for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
			if (stream->continuous_nodes[i] == stream->continuous_tail &&
			    current == (uintptr_t)stream->continuous_blocks[i] +
				      stream->i2s_cfg.block_size) {
				tail_end = true;
				if ((hdma->Instance->CCR & DMA_CCR_EN) == 0U &&
				    (hdma->Instance->CSR & DMA_CSR_IDLEF) != 0U) {
					terminal_end = true;
					terminal_index = i;
				}
			}
		}
		for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
			if (stream->continuous_node_state[i] != SAI_CONT_RX_NODE_DMA) {
				continue;
			}
			uintptr_t start = (uintptr_t)stream->continuous_blocks[i];
			uintptr_t end = start + stream->i2s_cfg.block_size;
			if (current == end) {
				boundary_end_index = i;
			}
			if (terminal_end) {
				continue;
			}
			if (current > start && current < end) {
				active_index = i;
				break;
			}
			if (current == start && stream->continuous_stop_pending) {
				active_index = i;
				break;
			}
			if (current == start || current == end) {
				ambiguous_boundary = true;
			}
		}
		uint8_t prefix_start = stream->continuous_next_complete;
		uint8_t publish_count = 0U;
		enum sai_rx_prefix_result plan;
		if (stream->continuous_stop_pending && terminal_end) {
			if (stream->continuous_node_state[terminal_index] == SAI_CONT_RX_NODE_DMA) {
				plan = sai_rx_plan_prefix_through(stream->continuous_next_complete,
							   (uint8_t)terminal_index,
							   SAI_CONT_RX_NODE_COUNT,
							   stream->continuous_node_state,
							   SAI_CONT_RX_NODE_DMA, &publish_count);
			} else {
				bool dma_owned = false;

				for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
					dma_owned |= stream->continuous_node_state[i] ==
						     SAI_CONT_RX_NODE_DMA;
				}
				plan = dma_owned ? SAI_RX_PREFIX_FAULT : SAI_RX_PREFIX_READY;
			}
		} else if (stream->continuous_stop_pending && tc != 0U && tail_end &&
			   active_index < 0) {
			/* An unproven finite end cannot be assigned to an aliased owner. */
			plan = SAI_RX_PREFIX_FAULT;
		} else if (stream->continuous_stop_pending && tc != 0U &&
			   active_index < 0 && boundary_end_index >= 0 &&
			   stream->continuous_nodes[boundary_end_index] != stream->continuous_tail) {
			plan = sai_rx_plan_prefix_through(stream->continuous_next_complete,
						   (uint8_t)boundary_end_index,
						   SAI_CONT_RX_NODE_COUNT,
						   stream->continuous_node_state,
						   SAI_CONT_RX_NODE_DMA, &publish_count);
		} else {
			/* At TC, CDAR can point at the first byte of the next block. */
			if (stream->continuous_stop_pending && tc != 0U && active_index < 0) {
				for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
					if (current == (uintptr_t)stream->continuous_blocks[i]) {
						active_index = i;
						ambiguous_boundary = false;
						break;
					}
				}
			}
			plan = sai_rx_plan_completed_prefix(
				stream->continuous_next_complete, (int8_t)active_index,
				SAI_CONT_RX_NODE_COUNT, stream->continuous_node_state,
				SAI_CONT_RX_NODE_DMA, ambiguous_boundary,
				(hdma->Instance->CCR & DMA_CCR_EN) != 0U, terminal_end,
				&publish_count);
		}
		if (plan == SAI_RX_PREFIX_DEFER) {
			return;
		}
		if (plan != SAI_RX_PREFIX_READY) {
			stream->continuous_fault = true;
			stream->state = I2S_STATE_ERROR;
			__HAL_DMA_DISABLE(hdma);
			sub_data->hsai.Instance->CR1 &= ~(SAI_xCR1_DMAEN | SAI_xCR1_SAIEN);
			return;
		}
		for (uint8_t completed = 0U; completed < publish_count; completed++) {
			uint8_t i = stream->continuous_next_complete;
			struct queue_item item = {
				.buffer = stream->continuous_blocks[i],
				.size = stream->i2s_cfg.block_size,
			};
			if (k_msgq_put(&stream->queue, &item, K_NO_WAIT) != 0) {
				stream->continuous_fault = true;
				stream->state = I2S_STATE_ERROR;
				__HAL_DMA_DISABLE(hdma);
				return;
			}
			stream->continuous_node_state[i] = SAI_CONT_RX_NODE_QUEUED;
			stream->continuous_next_complete = (uint8_t)((i + 1U) % SAI_CONT_RX_NODE_COUNT);
			uint32_t queue_depth = k_msgq_num_used_get(&stream->queue);
			if (queue_depth > (uint32_t)atomic_get(&stream->continuous_queue_high_water)) {
				atomic_set(&stream->continuous_queue_high_water, (atomic_val_t)queue_depth);
			}
		}
		if (stream->continuous_stop_pending &&
			sai_rx_prefix_contains(prefix_start, publish_count, SAI_CONT_RX_NODE_COUNT,
					       stream->continuous_stop_target)) {
			stream->continuous_stop_target_complete = true;
		}
		if (stream->continuous_stop_pending &&
		    stream->continuous_node_state[stream->continuous_stop_target] !=
			    SAI_CONT_RX_NODE_DMA) {
			stream->continuous_stop_target_complete = true;
		}
		if (stream->continuous_stop_pending && tc != 0U &&
		    stream->continuous_stop_target_complete) {
			/* Stop only at a DMA block boundary; finish HAL cleanup in thread context. */
			sub_data->hsai.Instance->CR1 &= ~(SAI_xCR1_DMAEN | SAI_xCR1_SAIEN);
			__HAL_DMA_DISABLE(hdma);
			stream->continuous_stop_pending = false;
			stream->continuous_stopped = true;
			stream->continuous_stop_target_complete = false;
			stream->state = I2S_STATE_READY;
		}
		return;
	}
#endif

	if (status < 0) {
		LOG_ERR("DMA callback error with channel %d.", channel);
	}
	HAL_DMA_IRQHandler(hdma);
}

#if defined(CONFIG_SOC_SERIES_STM32F4X)
static int stm32_sai_sub_f4_clk_src_conf(const struct device *dev)
{
	const struct stm32_sai_sub_cfg *sub_cfg = dev->config;
	const struct stm32_sai_cfg *sai_cfg = sub_cfg->controller->config;
	struct stm32_sai_sub_data *sub_data = dev->data;
	SAI_HandleTypeDef *hsai = &sub_data->hsai;
	uint32_t clock_source = 0U;

	if (sai_cfg->has_sai_ker_ck) {
		clock_source = sai_cfg->sai_ker_ck.bus;
	}

	switch (clock_source) {
#if defined(STM32F413xx) || defined(STM32F423xx)
	case STM32_SRC_PLLI2S_POST_R:
		hsai->Init.ClockSource = SAI_CLKSOURCE_PLLI2S;
		break;

	case STM32_SRC_PLL_POST_R:
		hsai->Init.ClockSource = SAI_CLKSOURCE_PLLR;
		break;

	case STM32_SRC_HSI:
		hsai->Init.ClockSource = SAI_CLKSOURCE_HS;
		break;
#else /* STM32F413xx || STM32F423xx */
	case STM32_SRC_PLLSAI_POST_Q:
		hsai->Init.ClockSource = SAI_CLKSOURCE_PLLSAI;
		break;

	case STM32_SRC_PLLI2S_POST_Q:
		hsai->Init.ClockSource = SAI_CLKSOURCE_PLLI2S;
		break;
#endif /* STM32F413xx || STM32F423xx */
	case 0U:
		/* No source clock defined. Do nothing. */
		break;

	default:
		LOG_ERR("Wrong source clock defined.");
		return -EINVAL;
	}

	return 0;
}
#endif /* CONFIG_SOC_SERIES_STM32F4X */

static int stm32_sai_sub_conf(const struct device *dev, enum i2s_dir dir,
				   const struct i2s_config *i2s_cfg)
{
	const struct stm32_sai_sub_cfg *const sub_cfg = dev->config;
	struct stm32_sai_sub_data *const sub_data = dev->data;
	struct stream *stream = &sub_data->stream;
	SAI_HandleTypeDef *hsai = &sub_data->hsai;
	uint8_t protocol;
	uint8_t word_size;
#if defined(CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6)
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		if (stream->continuous_blocks[i] != NULL ||
		    stream->continuous_node_state[i] != SAI_CONT_RX_NODE_FREE) {
			return -EBUSY;
		}
	}
#endif

	memcpy(&stream->i2s_cfg, i2s_cfg, sizeof(struct i2s_config));
#if defined(CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6)
	stream->continuous_rx = dir == I2S_DIR_RX && i2s_cfg->frame_clk_freq == 384000U &&
				i2s_cfg->word_size == 16U && i2s_cfg->channels == 2U &&
				i2s_cfg->block_size == 16384U;
	hsai->Init.FIFOThreshold = stream->continuous_rx ? SAI_FIFOTHRESHOLD_HF :
								SAI_FIFOTHRESHOLD_FULL;
#endif

	stream->master = true;
	if (i2s_cfg->options & I2S_OPT_FRAME_CLK_TARGET ||
	    i2s_cfg->options & I2S_OPT_BIT_CLK_TARGET) {
		stream->master = false;
	}

#if defined(CONFIG_SOC_SERIES_STM32F4X)
	int err;

	/* ON F4x, the HAL modifies the RCC to set the source clock, so it is necessary to define
	 * the ClockSource Init parameter.
	 */
	err = stm32_sai_sub_f4_clk_src_conf(dev);
	if (err != 0) {
		return err;
	}
#endif /* CONFIG_SOC_SERIES_STM32F4X */

	hsai->Init.Synchro = SAI_ASYNCHRONOUS;

	if (dir == I2S_DIR_RX) {
		if (sub_cfg->dir == I2S_DIR_TX) {
			LOG_ERR("Invalid direction, SAI configured as TX");
			return -EINVAL;
		}

		hsai->Init.AudioMode = SAI_MODEMASTER_RX;

		if (stream->master == false) {
			hsai->Init.AudioMode = SAI_MODESLAVE_RX;
			if (sub_cfg->synchronous) {
				hsai->Init.Synchro = SAI_SYNCHRONOUS;
				LOG_WRN("Synchronous RX peripheral mode requires an active "
					"controller with bit clock gating disabled");
			}
		}

	} else if (dir == I2S_DIR_TX) {
		if (sub_cfg->dir == I2S_DIR_RX) {
			LOG_ERR("Invalid direction, SAI configured as RX");
			return -EINVAL;
		}

		hsai->Init.AudioMode = SAI_MODEMASTER_TX;

		if (stream->master == false) {
			hsai->Init.AudioMode = SAI_MODESLAVE_TX;
			if (sub_cfg->synchronous) {
				hsai->Init.Synchro = SAI_SYNCHRONOUS;
				LOG_WRN("Synchronous TX peripheral mode requires an active "
					"controller with bit clock gating disabled");
			}
		}
	} else {
		LOG_ERR("Either RX or TX direction must be selected");
		return -EINVAL;
	}

	if (stream->state != I2S_STATE_NOT_READY && stream->state != I2S_STATE_READY) {
		LOG_ERR("Invalid state: %d", (int)stream->state);
		return -EINVAL;
	}

	/* MckOutput is not supported by all MCU series */
#if defined(SAI_MCK_OUTPUT_ENABLE)
	if (sub_cfg->mclk_enable && stream->master) {
		hsai->Init.MckOutput = SAI_MCK_OUTPUT_ENABLE;
	} else {
		hsai->Init.MckOutput = SAI_MCK_OUTPUT_DISABLE;
	}
#endif

	if (sub_cfg->mclk_div == (enum mclk_divider)MCLK_NO_DIV) {
		hsai->Init.NoDivider = SAI_MASTERDIVIDER_DISABLE;
	} else {
		hsai->Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;

		/* MckOverSampling is not supported by all MCU series */
#if defined(SAI_MCK_OVERSAMPLING_DISABLE)
		if (sub_cfg->mclk_div == (enum mclk_divider)MCLK_DIV_256) {
			hsai->Init.MckOverSampling = SAI_MCK_OVERSAMPLING_DISABLE;
		} else {
			hsai->Init.MckOverSampling = SAI_MCK_OVERSAMPLING_ENABLE;
		}
#endif
	}

	/* AudioFrequency */
	switch (stream->i2s_cfg.frame_clk_freq) {
#if defined(SAI_AUDIO_FREQUENCY_384K)
	case 384000U:
		hsai->Init.AudioFrequency = SAI_AUDIO_FREQUENCY_384K;
		break;
#endif
	case 192000U:
		hsai->Init.AudioFrequency = SAI_AUDIO_FREQUENCY_192K;
		break;
	case 96000U:
		hsai->Init.AudioFrequency = SAI_AUDIO_FREQUENCY_96K;
		break;
	case 48000U:
		hsai->Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;
		break;
	case 44100U:
		hsai->Init.AudioFrequency = SAI_AUDIO_FREQUENCY_44K;
		break;
	case 32000U:
		hsai->Init.AudioFrequency = SAI_AUDIO_FREQUENCY_32K;
		break;
	case 22050U:
		hsai->Init.AudioFrequency = SAI_AUDIO_FREQUENCY_22K;
		break;
	case 16000U:
		hsai->Init.AudioFrequency = SAI_AUDIO_FREQUENCY_16K;
		break;
	case 11025U:
		hsai->Init.AudioFrequency = SAI_AUDIO_FREQUENCY_11K;
		break;
	case 8000U:
		hsai->Init.AudioFrequency = SAI_AUDIO_FREQUENCY_8K;
		break;
	default:
		LOG_ERR("Invalid frame_clk_freq %u", stream->i2s_cfg.frame_clk_freq);
		stream->state = I2S_STATE_NOT_READY;
		return -EINVAL;
	}

	/* WordSize */
	switch (stream->i2s_cfg.word_size) {
	case 16:
		word_size = SAI_PROTOCOL_DATASIZE_16BIT;
		stream->dma_src_size = 2;
		break;
	case 24:
		word_size = SAI_PROTOCOL_DATASIZE_24BIT;
		stream->dma_src_size = 4;
		break;
	case 32:
		word_size = SAI_PROTOCOL_DATASIZE_32BIT;
		stream->dma_src_size = 4;
		break;
	default:
		LOG_ERR("Invalid wordsize %u", stream->i2s_cfg.word_size);
		stream->state = I2S_STATE_NOT_READY;
		return -EINVAL;
	}

	/* MonoStereoMode */
	switch (stream->i2s_cfg.channels) {
	case 1:
		hsai->Init.MonoStereoMode = SAI_MONOMODE;
		LOG_DBG("SAI_MONOMODE");
		break;
	case 2:
		hsai->Init.MonoStereoMode = SAI_STEREOMODE;
		LOG_DBG("SAI_STEREOMODE");
		break;
	default:
		LOG_ERR("NOT VALID CHANNEL NUMBER %u", stream->i2s_cfg.channels);
		stream->state = I2S_STATE_NOT_READY;
		return -EINVAL;
	}

	if (stream->i2s_cfg.options & I2S_OPT_PINGPONG) {
		LOG_ERR("Ping-pong mode not supported");
		stream->state = I2S_STATE_NOT_READY;
		return -ENOTSUP;
	}

	if ((stream->i2s_cfg.format & I2S_FMT_DATA_ORDER_LSB) ||
	    (stream->i2s_cfg.format & I2S_FMT_BIT_CLK_INV) ||
	    (stream->i2s_cfg.format & I2S_FMT_FRAME_CLK_INV)) {
		LOG_ERR("Unsupported stream format");
		return -EINVAL;
	}

	switch (stream->i2s_cfg.format & I2S_FMT_DATA_FORMAT_MASK) {
	case I2S_FMT_DATA_FORMAT_I2S:
		protocol = SAI_I2S_STANDARD;
		break;
	case I2S_FMT_DATA_FORMAT_LEFT_JUSTIFIED:
		protocol = SAI_I2S_MSBJUSTIFIED;
		break;
	case I2S_FMT_DATA_FORMAT_PCM_SHORT:
		protocol = SAI_PCM_SHORT;
		break;
	case I2S_FMT_DATA_FORMAT_PCM_LONG:
		protocol = SAI_PCM_LONG;
		break;
	case I2S_FMT_DATA_FORMAT_RIGHT_JUSTIFIED:
		protocol = SAI_I2S_LSBJUSTIFIED;
		break;
	default:
		LOG_ERR("Unsupported I2S data format");
		return -EINVAL;
	}

	/* Initialize SAI peripheral */
	if (HAL_SAI_InitProtocol(hsai, protocol, word_size, 2) != HAL_OK) {
		LOG_ERR("HAL_SAI_InitProtocol: <FAILED>");
		return -EIO;
	}

	stream->state = I2S_STATE_READY;

	/*
	 * Enable immediately SAI peripheral only when the bit clock is not gated.
	 * It allows the synchronous sub-block to become operational immediately.
	 */
	if (((i2s_cfg->options & I2S_OPT_BIT_CLK_GATED) == 0) &&
	    hsai->Init.Synchro != SAI_SYNCHRONOUS) {

		__HAL_SAI_ENABLE(hsai);

		if (sub_cfg->dir == I2S_DIR_TX) {
			/* Prime the FIFO with a dummy sample so the controller
			 * actually starts clocking out frames.
			 */
			stm32_reg_write(&hsai->Instance->DR, 0U);
		} else {
			/* Discard whatever's in DR to clear FIFO state
			 * before the real DMA-driven reads begin.
			 */
			(void)stm32_reg_read(&hsai->Instance->DR);
		}

		LOG_DBG("SAI Enabled");
	}

	return 0;
}

static int stm32_sai_sub_write(const struct device *dev, void *mem_block, size_t size)
{
	const struct stm32_sai_sub_cfg *const sub_cfg = dev->config;
	struct stm32_sai_sub_data *sub_data = dev->data;
	struct stream *stream = &sub_data->stream;
	int ret;

	if (sub_cfg->dir == I2S_DIR_RX) {
		LOG_ERR("Invalid operation, SAI configured as RX");
		return -EIO;
	}

	if (stream->state != I2S_STATE_RUNNING && stream->state != I2S_STATE_READY) {
		LOG_ERR("TX Invalid state: %d", stream->state);
		return -EIO;
	}

	if (size > stream->i2s_cfg.block_size) {
		LOG_ERR("Max write size is: %zu", stream->i2s_cfg.block_size);
		return -EINVAL;
	}

	struct queue_item item = {.buffer = mem_block, .size = size};

	ret = k_msgq_put(&stream->queue, &item, K_MSEC(stream->i2s_cfg.timeout));
	if (ret < 0) {
		LOG_ERR("TX queue full");
		return ret;
	}

	return 0;
}

static int stm32_sai_sub_read(const struct device *dev, void **mem_block, size_t *size)
{
	const struct stm32_sai_sub_cfg *const sub_cfg = dev->config;
	struct stm32_sai_sub_data *sub_data = dev->data;
	struct stream *stream = &sub_data->stream;
	struct queue_item item;
	int ret;

	if (sub_cfg->dir == I2S_DIR_TX) {
		LOG_ERR("Invalid operation, SAI configured as TX");
		return -EIO;
	}

	if (stream->state == I2S_STATE_NOT_READY || stream->state == I2S_STATE_ERROR) {
		LOG_ERR("RX invalid state: %d", (int)stream->state);
		return -EIO;
	}

	ret = k_msgq_get(&stream->queue, &item, K_MSEC(stream->i2s_cfg.timeout));
	if (ret < 0) {
	#if defined(CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6)
		if (stream->continuous_rx) {
			uint32_t sai_status = sub_data->hsai.Instance->SR &
				(SAI_xSR_OVRUDR | SAI_xSR_WCKCFG | SAI_xSR_AFSDET | SAI_xSR_LFSDET);
			if (sai_status != 0U) {
				stream->continuous_fault = true;
				stream->state = I2S_STATE_ERROR;
				__HAL_DMA_DISABLE(&sub_data->hdma);
				sub_data->hsai.Instance->CR1 &= ~(SAI_xCR1_DMAEN | SAI_xCR1_SAIEN);
				LOG_ERR("SAI continuous RX fault: status=%08x dma=%08x",
					(unsigned int)sai_status,
					(unsigned int)__HAL_DMA_GET_FLAG(&sub_data->hdma,
						DMA_FLAG_TC | DMA_FLAG_HT | DMA_FLAG_DTE |
						DMA_FLAG_ULE | DMA_FLAG_USE | DMA_FLAG_TO | DMA_FLAG_SUSP));
				return -EIO;
			}
		}
	#endif
		LOG_ERR("RX queue: %d", k_msgq_num_used_get(&stream->queue));
		return ret;
	}
#if defined(CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6)
	if (stream->continuous_rx) {
		bool found = false;
		for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
			if (stream->continuous_blocks[i] == item.buffer &&
			    stream->continuous_node_state[i] == SAI_CONT_RX_NODE_QUEUED) {
				stream->continuous_node_state[i] = SAI_CONT_RX_NODE_BORROWED;
				found = true;
				break;
			}
		}
		if (!found) {
			stream->continuous_fault = true;
			stream->state = I2S_STATE_ERROR;
			return -EIO;
		}
	}
#endif

	*mem_block = item.buffer;
	*size = item.size;

	return 0;
}

#if defined(CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6)
static int continuous_rx_start(const struct device *dev)
{
	struct stm32_sai_sub_data *sub_data = dev->data;
	struct stream *stream = &sub_data->stream;
	DMA_HandleTypeDef *hdma = &sub_data->hdma;
	SAI_HandleTypeDef *hsai = &sub_data->hsai;
	uintptr_t node_base = (uintptr_t)stream->continuous_nodes[0];
	uintptr_t node_end = (uintptr_t)stream->continuous_nodes[SAI_CONT_RX_NODE_COUNT - 1U] +
			     sizeof(struct sai_continuous_node_storage);

	if ((node_base & 31U) != 0U ||
	    ((node_base & DMA_CLBAR_LBA) != ((node_end - 1U) & DMA_CLBAR_LBA))) {
		return -EINVAL;
	}
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		if (stream->continuous_node_state[i] != SAI_CONT_RX_NODE_FREE ||
		    stream->continuous_blocks[i] != NULL) {
			return -EBUSY;
		}
	}

	memset(&stream->continuous_queue, 0, sizeof(stream->continuous_queue));
	stream->continuous_queue.Type = QUEUE_TYPE_STATIC;
	stream->continuous_queue.State = HAL_DMA_QUEUE_STATE_READY;
	stream->continuous_queue.NodeNumber = SAI_CONT_RX_NODE_COUNT;
	stream->continuous_queue.Head = stream->continuous_nodes[0];
	stream->continuous_queue.FirstCircularNode = NULL;
	stream->continuous_next_complete = 0U;
	stream->continuous_next_release = 0U;
	atomic_set(&stream->continuous_queue_high_water, 0);
	stream->continuous_slab_high_water = 0U;
	stream->continuous_fault = false;
	stream->continuous_quiesced = false;
	stream->continuous_stop_pending = false;
	stream->continuous_stopped = false;
	stream->continuous_stop_target_complete = false;
	stream->continuous_stop_target = 0U;

	hdma->InitLinkedList.Priority = hdma->Init.Priority;
	hdma->InitLinkedList.LinkStepMode = DMA_LSM_FULL_EXECUTION;
	hdma->InitLinkedList.TransferEventMode = DMA_TCEM_EACH_LL_ITEM_TRANSFER;
	hdma->InitLinkedList.LinkedListMode = DMA_LINKEDLIST_NORMAL;
	hdma->InitLinkedList.LinkAllocatedPort = DMA_LINK_ALLOCATED_PORT1;
	if (HAL_DMAEx_List_Init(hdma) != HAL_OK) {
		return -EIO;
	}
	hdma->Parent = hsai;
	if (HAL_DMA_ConfigChannelAttributes(hdma, DMA_CHANNEL_SEC | DMA_CHANNEL_PRIV |
					    DMA_CHANNEL_SRC_SEC | DMA_CHANNEL_DEST_SEC) != HAL_OK) {
		goto free_blocks;
	}

	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		memset(&sub_data->continuous_node_storage[i], 0,
		       sizeof(sub_data->continuous_node_storage[i]));
		DMA_NodeConfTypeDef conf = {0};
		if (k_mem_slab_alloc(stream->i2s_cfg.mem_slab, &stream->continuous_blocks[i],
				     K_NO_WAIT) != 0) {
			goto free_blocks;
		}
		conf.NodeType = DMA_GPDMA_LINEAR_NODE;
		conf.Init = hdma->Init;
		conf.Init.TransferEventMode = DMA_TCEM_EACH_LL_ITEM_TRANSFER;
		conf.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT1;
#if defined(CPU_IN_SECURE_STATE)
		conf.SrcSecure = DMA_CHANNEL_SRC_SEC;
		conf.DestSecure = DMA_CHANNEL_DEST_SEC;
#endif
		conf.SrcAddress = (uint32_t)&hsai->Instance->DR;
		conf.DstAddress = (uint32_t)stream->continuous_blocks[i];
		conf.DataSize = stream->i2s_cfg.block_size;
		if (HAL_DMAEx_List_BuildNode(&conf, stream->continuous_nodes[i]) != HAL_OK) {
			k_mem_slab_free(stream->i2s_cfg.mem_slab, stream->continuous_blocks[i]);
			stream->continuous_blocks[i] = NULL;
			goto free_blocks;
		}
		stream->continuous_node_state[i] = SAI_CONT_RX_NODE_DMA;
		stream->continuous_slab_high_water = (uint32_t)i + 1U;
	}

	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		uint32_t next = (i + 1U < SAI_CONT_RX_NODE_COUNT)
				? ((uint32_t)stream->continuous_nodes[i + 1U] & DMA_CLLR_LA)
				: 0U;
		stream->continuous_link_mask = SAI_CONT_RX_LINK_UPDATE_MASK;
		stream->continuous_nodes[i]->LinkRegisters[5] = next | (next != 0U ?
										  SAI_CONT_RX_LINK_UPDATE_MASK : 0U);
	}
	stream->continuous_tail = stream->continuous_nodes[SAI_CONT_RX_NODE_COUNT - 1U];
	if (HAL_DMAEx_List_LinkQ(hdma, &stream->continuous_queue) != HAL_OK) {
		goto free_blocks;
	}
	__HAL_SAI_CLEAR_FLAG(hsai, SAI_FLAG_OVRUDR | SAI_FLAG_WCKCFG |
				   SAI_FLAG_AFSDET | SAI_FLAG_LFSDET);
	hsai->pBuffPtr = stream->continuous_blocks[0];
	hsai->XferSize = stream->i2s_cfg.block_size / stream->dma_src_size;
	hsai->XferCount = hsai->XferSize;
	hsai->ErrorCode = HAL_SAI_ERROR_NONE;
	hsai->State = HAL_SAI_STATE_BUSY_RX;
	hdma->XferHalfCpltCallback = NULL;
	hdma->XferCpltCallback = NULL;
	hdma->XferErrorCallback = NULL;
	hdma->XferSuspendCallback = NULL;
	__DSB();
	if (HAL_DMAEx_List_Start_IT(hdma) != HAL_OK) {
		if ((hdma->Instance->CCR & DMA_CCR_EN) != 0U) {
			stream->continuous_fault = true;
			stream->state = I2S_STATE_ERROR;
			return -EIO;
		}
		hsai->State = HAL_SAI_STATE_READY;
		goto free_blocks;
	}
	__HAL_DMA_ENABLE_IT(hdma, DMA_IT_HT);
	hsai->Instance->CR1 |= SAI_xCR1_DMAEN;
	if ((hsai->Instance->CR1 & SAI_xCR1_SAIEN) == 0U) {
		__HAL_SAI_ENABLE(hsai);
	}
	return 0;

free_blocks:
	(void)HAL_DMAEx_List_DeInit(hdma);
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		if (stream->continuous_blocks[i] != NULL) {
			k_mem_slab_free(stream->i2s_cfg.mem_slab, stream->continuous_blocks[i]);
			stream->continuous_blocks[i] = NULL;
		}
		stream->continuous_node_state[i] = SAI_CONT_RX_NODE_FREE;
	}
	return -EIO;
}

static int continuous_rx_quiesce(const struct device *dev, bool preserve_queue)
{
	struct stm32_sai_sub_data *sub_data = dev->data;
	struct stream *stream = &sub_data->stream;
	DMA_HandleTypeDef *hdma = &sub_data->hdma;
	SAI_HandleTypeDef *hsai = &sub_data->hsai;
	struct queue_item item;
	uint32_t interrupt_mask = DMA_IT_TC | DMA_IT_HT | DMA_IT_DTE | DMA_IT_ULE |
				  DMA_IT_USE | DMA_IT_TO | DMA_IT_SUSP;
	uint32_t flag_mask = DMA_FLAG_TC | DMA_FLAG_HT | DMA_FLAG_DTE | DMA_FLAG_ULE |
			     DMA_FLAG_USE | DMA_FLAG_TO | DMA_FLAG_SUSP;

	if (!stream->continuous_quiesced) {
		__HAL_DMA_DISABLE_IT(hdma, interrupt_mask);
		if ((hdma->Instance->CCR & DMA_CCR_EN) != 0U) {
			if (HAL_SAI_DMAStop(hsai) != HAL_OK) {
				stream->state = I2S_STATE_ERROR;
				stream->continuous_fault = true;
				return -EIO;
			}
		} else {
			hsai->Instance->CR1 &= ~SAI_xCR1_DMAEN;
			__HAL_SAI_DISABLE(hsai);
			if ((hsai->Instance->CR1 & SAI_xCR1_SAIEN) != 0U) {
				stream->state = I2S_STATE_ERROR;
				stream->continuous_fault = true;
				return -EIO;
			}
			hsai->State = HAL_SAI_STATE_READY;
		}
		if ((hdma->Instance->CCR & DMA_CCR_EN) != 0U ||
		    (hsai->Instance->CR1 & SAI_xCR1_SAIEN) != 0U ||
		    (hdma->Instance->CSR & DMA_CSR_IDLEF) == 0U ||
		    HAL_DMAEx_List_DeInit(hdma) != HAL_OK) {
			stream->state = I2S_STATE_ERROR;
			stream->continuous_fault = true;
			return -EIO;
		}
		__HAL_DMA_CLEAR_FLAG(hdma, flag_mask);
	}
	stream->continuous_quiesced = true;
	stream->continuous_stop_pending = false;
	stream->continuous_stop_target_complete = false;
	if (!preserve_queue) {
		while (k_msgq_get(&stream->queue, &item, K_NO_WAIT) == 0) {
			for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
				if (stream->continuous_blocks[i] == item.buffer &&
				    stream->continuous_node_state[i] == SAI_CONT_RX_NODE_QUEUED) {
					stream->continuous_node_state[i] = SAI_CONT_RX_NODE_DMA;
				}
			}
		}
	}
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		if (stream->continuous_node_state[i] != SAI_CONT_RX_NODE_BORROWED &&
		    (!preserve_queue || stream->continuous_node_state[i] != SAI_CONT_RX_NODE_QUEUED) &&
		    stream->continuous_blocks[i] != NULL) {
			k_mem_slab_free(stream->i2s_cfg.mem_slab, stream->continuous_blocks[i]);
			stream->continuous_blocks[i] = NULL;
			stream->continuous_node_state[i] = SAI_CONT_RX_NODE_FREE;
			stream->continuous_nodes[i]->LinkRegisters[5] = 0U;
		}
	}
	stream->continuous_stopped = false;
	return 0;
}

static uint8_t continuous_rx_stop_target(const struct stream *stream, uintptr_t current)
{
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		uintptr_t start = (uintptr_t)stream->continuous_blocks[i];
		uintptr_t end = start + stream->i2s_cfg.block_size;
		if (stream->continuous_node_state[i] == SAI_CONT_RX_NODE_DMA &&
		    current >= start && current < end) {
			return i;
		}
	}
	for (uint8_t i = 0U; i < SAI_CONT_RX_NODE_COUNT; i++) {
		uintptr_t end = (uintptr_t)stream->continuous_blocks[i] +
				stream->i2s_cfg.block_size;
		if (stream->continuous_node_state[i] == SAI_CONT_RX_NODE_DMA && current == end) {
			uint32_t next = stream->continuous_nodes[i]->LinkRegisters[5] & DMA_CLLR_LA;

			for (uint8_t j = 0U; j < SAI_CONT_RX_NODE_COUNT; j++) {
				if (stream->continuous_node_state[j] == SAI_CONT_RX_NODE_DMA &&
				    ((uint32_t)stream->continuous_nodes[j] & DMA_CLLR_LA) == next &&
				    next != 0U) {
					return j;
				}
			}
			return i;
		}
	}
	for (uint8_t offset = 0U; offset < SAI_CONT_RX_NODE_COUNT; offset++) {
		uint8_t i = (uint8_t)((stream->continuous_next_complete + offset) %
				      SAI_CONT_RX_NODE_COUNT);

		if (stream->continuous_node_state[i] == SAI_CONT_RX_NODE_DMA) {
			return i;
		}
	}
	return stream->continuous_next_complete;
}
#endif

static int stream_start(const struct device *dev, enum i2s_dir dir)
{
	struct stm32_sai_sub_data *sub_data = dev->data;
	struct stream *stream = &sub_data->stream;
	SAI_HandleTypeDef *hsai = &sub_data->hsai;
	struct queue_item item;
	int ret;

	if (dir == I2S_DIR_TX) {
		ret = k_msgq_get(&stream->queue, &item, K_NO_WAIT);
		if (ret < 0) {
			return -ENOMEM;
		}

		stream->mem_block = item.buffer;
		stream->mem_block_len = item.size;

		sys_cache_data_flush_range(stream->mem_block, stream->mem_block_len);

		if (HAL_SAI_Transmit_DMA(hsai, stream->mem_block,
					 stream->mem_block_len / stream->dma_src_size) != HAL_OK) {
			LOG_ERR("HAL_SAI_Transmit_DMA: <FAILED>");
			return -EIO;
		}
	} else {
#if defined(CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6)
		if (stream->continuous_rx) {
			return continuous_rx_start(dev);
		}
#endif

		ret = k_mem_slab_alloc(stream->i2s_cfg.mem_slab, &stream->mem_block, K_NO_WAIT);
		if (ret < 0) {
			return -ENOMEM;
		}

		stream->mem_block_len = stream->i2s_cfg.block_size;

		if (HAL_SAI_Receive_DMA(hsai, stream->mem_block,
					stream->mem_block_len / stream->dma_src_size) != HAL_OK) {
			LOG_ERR("HAL_SAI_Receive_DMA: <FAILED>");
			return -EIO;
		}
	}

	return 0;
}

static void queue_drop(const struct device *dev)
{
	struct stm32_sai_sub_data *sub_data = dev->data;
	struct stream *stream = &sub_data->stream;
	struct queue_item item;
#if defined(CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6)
	if (stream->continuous_rx) {
		return;
	}
#endif

	if (stream->mem_block != NULL) {
		stream->mem_block = NULL;
		stream->mem_block_len = 0;
	}

	while (k_msgq_get(&stream->queue, &item, K_NO_WAIT) == 0) {
		LOG_DBG("Dropping item from queue");
		k_mem_slab_free(stream->i2s_cfg.mem_slab, item.buffer);
	}
}

#if defined(CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6)
int stm32_sai_rx_release(const struct device *dev, void *buffer)
{
	struct stm32_sai_sub_data *sub_data;
	struct stream *stream;
	uint8_t index;
	unsigned int key;

	if (dev == NULL || buffer == NULL) {
		return -EINVAL;
	}
	sub_data = dev->data;
	stream = &sub_data->stream;
	if (!stream->continuous_rx) {
		return -ENOTSUP;
	}
	for (index = 0U; index < SAI_CONT_RX_NODE_COUNT; index++) {
		if (stream->continuous_blocks[index] == buffer) {
			break;
		}
	}
	if (index == SAI_CONT_RX_NODE_COUNT ||
	    stream->continuous_node_state[index] != SAI_CONT_RX_NODE_BORROWED) {
		return -EINVAL;
	}

	key = irq_lock();
	if (!sai_rx_release_is_ordered(index, stream->continuous_next_release,
				       stream->continuous_node_state[index],
				       SAI_CONT_RX_NODE_BORROWED,
				       stream->continuous_blocks[index] == buffer)) {
		irq_unlock(key);
		return -EINVAL;
	}
	if (stream->continuous_quiesced || stream->continuous_stopped) {
		if (!sai_rx_release_quiesced(index, SAI_CONT_RX_NODE_COUNT,
					     &stream->continuous_next_release,
					     stream->continuous_node_state,
					     SAI_CONT_RX_NODE_BORROWED,
					     SAI_CONT_RX_NODE_FREE,
					     stream->continuous_blocks, buffer)) {
			irq_unlock(key);
			return -EINVAL;
		}
		k_mem_slab_free(stream->i2s_cfg.mem_slab, buffer);
		if (stream->continuous_quiesced) {
			stream->continuous_nodes[index]->LinkRegisters[5] = 0U;
		}
		irq_unlock(key);
		return 0;
	}
	if (stream->continuous_fault || stream->state != I2S_STATE_RUNNING) {
		irq_unlock(key);
		return -EIO;
	}
	if (index != stream->continuous_next_release) {
		irq_unlock(key);
		return -EINVAL;
	}
	stream->continuous_nodes[index]->LinkRegisters[5] = 0U;
	__DSB();
	stream->continuous_tail->LinkRegisters[5] =
		((uint32_t)stream->continuous_nodes[index] & DMA_CLLR_LA) |
		stream->continuous_link_mask;
	__DSB();
	if ((sub_data->hdma.Instance->CCR & DMA_CCR_EN) == 0U) {
		stream->continuous_tail->LinkRegisters[5] = 0U;
		__DSB();
		stream->continuous_fault = true;
		stream->state = I2S_STATE_ERROR;
		__HAL_DMA_DISABLE(&sub_data->hdma);
		irq_unlock(key);
		return -EIO;
	}
	stream->continuous_tail = stream->continuous_nodes[index];
	stream->continuous_node_state[index] = SAI_CONT_RX_NODE_DMA;
	stream->continuous_next_release = (uint8_t)((index + 1U) % SAI_CONT_RX_NODE_COUNT);
	irq_unlock(key);
	return 0;
}

int stm32_sai_rx_get_stats(const struct device *dev, uint32_t *queue_high_water,
			   uint32_t *queue_capacity, uint32_t *slab_high_water,
			   uint32_t *slab_capacity)
{
	struct stm32_sai_sub_data *sub_data;
	struct stream *stream;

	if (dev == NULL || queue_high_water == NULL || queue_capacity == NULL ||
	    slab_high_water == NULL || slab_capacity == NULL) {
		return -EINVAL;
	}
	sub_data = dev->data;
	stream = &sub_data->stream;
	if (!stream->continuous_rx) {
		return -ENOTSUP;
	}
	*queue_high_water = (uint32_t)atomic_get(&stream->continuous_queue_high_water);
	*queue_capacity = ARRAY_SIZE(sub_data->queue_buffer);
	*slab_high_water = stream->continuous_slab_high_water;
	*slab_capacity = SAI_CONT_RX_NODE_COUNT;
	return 0;
}
#endif

static int stm32_sai_sub_trigger(const struct device *dev, enum i2s_dir dir,
				 enum i2s_trigger_cmd cmd)
{
	struct stm32_sai_sub_data *sub_data = dev->data;
	struct stream *stream = &sub_data->stream;
	unsigned int key;
	int ret;

	if (dir == I2S_DIR_BOTH) {
		LOG_ERR("Unsupported direction: %d", (int)dir);
		return -ENOTSUP;
	}

	switch (cmd) {
	case I2S_TRIGGER_START:
		LOG_DBG("I2S_TRIGGER_START");

		if (stream->state != I2S_STATE_READY) {
			LOG_ERR("START trigger: invalid state %d", stream->state);
			return -EIO;
		}
#if defined(CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6)
		if (stream->continuous_rx && stream->continuous_stopped) {
			ret = continuous_rx_quiesce(dev, true);
			if (ret != 0) {
				return ret;
			}
		}
#endif

		ret = stream->stream_start(dev, dir);
		if (ret < 0) {
			LOG_ERR("START trigger failed %d", ret);
			return ret;
		}

		stream->state = I2S_STATE_RUNNING;
		stream->last_block = false;

		break;
	case I2S_TRIGGER_STOP:
		key = irq_lock();
		LOG_DBG("I2S_TRIGGER_STOP");

		if (stream->state != I2S_STATE_RUNNING) {
			LOG_ERR("STOP - Invalid state: %d", (int)stream->state);
			irq_unlock(key);
			return -EIO;
		}

		stream->last_block = true;
		stream->state = I2S_STATE_STOPPING;
#if defined(CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6)
		if (stream->continuous_rx) {
			stream->continuous_stop_pending = true;
			stream->continuous_stop_target = continuous_rx_stop_target(
				stream, sub_data->hdma.Instance->CDAR);
			stream->continuous_stop_target_complete =
				stream->continuous_node_state[stream->continuous_stop_target] !=
				SAI_CONT_RX_NODE_DMA;
		}
#endif

		irq_unlock(key);
		break;
	case I2S_TRIGGER_DRAIN:
		key = irq_lock();
		LOG_DBG("I2S_TRIGGER_DRAIN");

		if (stream->state != I2S_STATE_RUNNING) {
			LOG_ERR("DRAIN - Invalid state: %d", (int)stream->state);
			irq_unlock(key);
			return -EIO;
		}

		stream->state = I2S_STATE_STOPPING;
#if defined(CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6)
		if (stream->continuous_rx) {
			stream->continuous_stop_pending = true;
			stream->continuous_stop_target = continuous_rx_stop_target(
				stream, sub_data->hdma.Instance->CDAR);
			stream->continuous_stop_target_complete =
				stream->continuous_node_state[stream->continuous_stop_target] !=
				SAI_CONT_RX_NODE_DMA;
		}
#endif

		irq_unlock(key);
		break;
	case I2S_TRIGGER_DROP:
		key = irq_lock();
		LOG_DBG("I2S_TRIGGER_DROP");

		if (stream->state == I2S_STATE_NOT_READY) {
			LOG_ERR("DROP - invalid state: %d", (int)stream->state);
			irq_unlock(key);
			return -EIO;
		}
#if defined(CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6)
		if (stream->continuous_rx) {
			stream->continuous_stop_pending = false;
			stream->continuous_stop_target_complete = false;
		}
		if (stream->continuous_rx) {
			stream->state = I2S_STATE_STOPPING;
			irq_unlock(key);
			ret = continuous_rx_quiesce(dev, false);
			if (ret != 0) {
				return ret;
			}
			key = irq_lock();
		}
#endif

		stream->queue_drop(dev);
		stream->state = I2S_STATE_READY;

		irq_unlock(key);
		break;
	case I2S_TRIGGER_PREPARE:
		key = irq_lock();
		LOG_DBG("I2S_TRIGGER_PREPARE");

		if (stream->state != I2S_STATE_ERROR) {
			LOG_ERR("PREPARE - invalid state: %d", (int)stream->state);
			irq_unlock(key);
			return -EIO;
		}
#if defined(CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6)
		if (stream->continuous_rx) {
			stream->state = I2S_STATE_STOPPING;
			irq_unlock(key);
			ret = continuous_rx_quiesce(dev, false);
			if (ret != 0) {
				return ret;
			}
			key = irq_lock();
		}
#endif
		stream->queue_drop(dev);
		stream->state = I2S_STATE_READY;

		irq_unlock(key);
		break;
	default:
		LOG_ERR("Unsupported trigger command");
		return -EINVAL;
	}
	return 0;
}

static int sai_init(const struct device *dev)
{
	int ret = 0;

	/* Enable SAI clock */
	ret = stm32_sai_clock_en(dev);
	if (ret < 0) {
		LOG_ERR("SAI clock enable <FAILED>, ret: %d.", ret);
		return -EIO;
	}

	LOG_DBG("%s Init: <OK>", dev->name);

	return 0;
}

static const struct i2s_config *stm32_sai_sub_conf_get(const struct device *dev, enum i2s_dir dir)
{
	const struct stm32_sai_sub_cfg *const sub_cfg = dev->config;
	struct stm32_sai_sub_data *sub_data = dev->data;
	struct stream *stream = &sub_data->stream;

	if (sub_cfg->dir != dir) {
		LOG_WRN("Direction mismatch: requested %d, sub-block configured as %d", dir,
			sub_cfg->dir);
		return NULL;
	}

	if (stream != NULL && stream->state != I2S_STATE_NOT_READY) {
		return &stream->i2s_cfg;
	}

	return NULL;
}

static DEVICE_API(i2s, i2s_stm32_sai_api) = {
	.configure = stm32_sai_sub_conf,
	.trigger = stm32_sai_sub_trigger,
	.write = stm32_sai_sub_write,
	.read = stm32_sai_sub_read,
	.config_get = stm32_sai_sub_conf_get,
};

#define SAI_FIFO_THRESHOLD(node) sai_fifo_threshold[DT_ENUM_IDX(node, fifo_threshold)]

#define SAI_SUB_DMA_CHANNEL_INIT(node, src, dest)                                                  \
	.stream = {                                                                                \
		.dma_dev = DEVICE_DT_GET(DT_DMAS_CTLR(node)),                                      \
		.dma_channel = DT_DMAS_CELL_BY_IDX(node, 0, channel),                              \
		.reg = (DMA_TypeDef *)DT_REG_ADDR(DT_PHANDLE_BY_IDX(node, dmas, 0)),               \
		.dma_cfg = {                                                                       \
			.dma_slot = STM32_DT_DMA_SLOT_BY_IDX(node, 0),                             \
			.channel_direction = src##_TO_##dest,                                      \
			.dma_callback = dma_callback,                                              \
			.channel_priority = STM32_DMA_CONFIG_PRIORITY(                             \
				STM32_DT_DMA_CHANNEL_CONFIG_BY_IDX(node, 0)),                      \
			.source_data_size = STM32_DMA_CONFIG_##src##_DATA_SIZE(                    \
				STM32_DT_DMA_CHANNEL_CONFIG_BY_IDX(node, 0)),                      \
			.dest_data_size = STM32_DMA_CONFIG_##dest##_DATA_SIZE(                     \
				STM32_DT_DMA_CHANNEL_CONFIG_BY_IDX(node, 0)),                      \
		},                                                                                 \
		.stream_start = stream_start,                                                      \
		.queue_drop = queue_drop,                                                          \
	}

#define SAI_SUB_INIT(node)                                                                         \
	PINCTRL_DT_DEFINE(node);                                                                   \
	SAI_CONT_NODE_STORAGE_DECL(node)                                                            \
                                                                                                   \
	static struct stm32_sai_sub_data sub_data_##node = {                                       \
		.hsai = {                                                                          \
			.Instance = (SAI_Block_TypeDef *)DT_REG_ADDR(node),                        \
			.Init.OutputDrive = SAI_OUTPUTDRIVE_DISABLE,                               \
			.Init.FIFOThreshold = SAI_FIFO_THRESHOLD(node),                            \
			.Init.SynchroExt = SAI_SYNCEXT_DISABLE,                                    \
			.Init.CompandingMode = SAI_NOCOMPANDING,                                   \
			.Init.TriState = SAI_OUTPUT_NOTRELEASED,                                   \
		},                                                                                 \
		COND_CODE_1(DT_DMAS_HAS_NAME(node, tx),                                            \
		    (SAI_SUB_DMA_CHANNEL_INIT(node, MEMORY, PERIPHERAL)),                          \
		    (SAI_SUB_DMA_CHANNEL_INIT(node, PERIPHERAL, MEMORY))),                         \
		SAI_CONT_NODE_STORAGE_INIT(node)                                                   \
	};                                                                                         \
                                                                                                   \
	static const struct stm32_sai_sub_cfg sub_cfg_##node = {                                   \
		.pincfg = PINCTRL_DT_DEV_CONFIG_GET(node),                                         \
		.mclk_enable = DT_PROP(node, mclk_enable),                                         \
		.mclk_div = DT_ENUM_IDX(node, mclk_divider),                                       \
		.synchronous = DT_PROP(node, synchronous),                                         \
		.controller = DEVICE_DT_GET(DT_PARENT(node)),                                      \
		.dir = COND_CODE_1(DT_DMAS_HAS_NAME(node, tx), (I2S_DIR_TX), (I2S_DIR_RX)),        \
	};                                                                                         \
	DEVICE_DT_DEFINE(node, &sai_sub_init, NULL, &sub_data_##node, &sub_cfg_##node,             \
			 POST_KERNEL, CONFIG_I2S_INIT_PRIORITY, &i2s_stm32_sai_api);

#define SAI_KER_CK_FIELD_INIT(inst, n)                                                             \
	COND_CODE_1(DT_INST_CLOCKS_HAS_NAME(inst, n),                                              \
		(.n = STM32_DT_INST_CLOCK_INFO_BY_NAME(inst, n), .has_##n = true,),                \
		(.has_##n = false,))

#define SAI_KER_CK_INIT(inst)                                                                      \
	SAI_KER_CK_FIELD_INIT(inst, sai_ker_ck)                                                    \
	SAI_KER_CK_FIELD_INIT(inst, sai_b_ker_ck)

/* Controller Node */
#define SAI_INIT(inst)                                                                             \
	static const struct stm32_sai_cfg sai_cfg_##inst = {                                       \
		.sai_ck = STM32_DT_INST_CLOCK_INFO_BY_NAME(inst, sai_ck),                          \
		SAI_KER_CK_INIT(inst)                                                              \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, &sai_init, NULL, NULL, &sai_cfg_##inst, POST_KERNEL,           \
			      CONFIG_I2S_INIT_PRIORITY, NULL);                                     \
                                                                                                   \
	DT_INST_FOREACH_CHILD_STATUS_OKAY(inst, SAI_SUB_INIT)

DT_INST_FOREACH_STATUS_OKAY(SAI_INIT)
