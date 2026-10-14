/*
 * Copyright (c) 2026 Hula Firmware
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef I2S_STM32_SAI_DRIVER_FIXTURE_SUPPORT_H_
#define I2S_STM32_SAI_DRIVER_FIXTURE_SUPPORT_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/device.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "i2s_stm32_sai_rx_ring.h"

static int continuous_rx_quiesce(const struct device *dev, bool preserve_queue);

#define CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6 1
#define CONFIG_SOC_SERIES_STM32N6X 1
#define CONFIG_I2S_STM32_SAI_BLOCK_COUNT 8
#define SAI_CONT_RX_NODE_COUNT 8U
#define SAI_CONT_RX_NODE_FREE 0U
#define SAI_CONT_RX_NODE_DMA 1U
#define SAI_CONT_RX_NODE_QUEUED 2U
#define SAI_CONT_RX_NODE_BORROWED 3U
#define SAI_CONT_RX_LINK_UPDATE_MASK 0x3fU

#define DMA_FLAG_TC BIT(0)
#define DMA_FLAG_HT BIT(1)
#define DMA_FLAG_DTE BIT(2)
#define DMA_FLAG_ULE BIT(3)
#define DMA_FLAG_USE BIT(4)
#define DMA_FLAG_TO BIT(5)
#define DMA_FLAG_SUSP BIT(6)
#define DMA_IT_TC BIT(0)
#define DMA_IT_HT BIT(1)
#define DMA_IT_DTE BIT(2)
#define DMA_IT_ULE BIT(3)
#define DMA_IT_USE BIT(4)
#define DMA_IT_TO BIT(5)
#define DMA_IT_SUSP BIT(6)
#define DMA_CCR_EN BIT(0)
#define DMA_CCR_LSM BIT(8)
#define DMA_CCR_SUSP BIT(1)
#define DMA_CCR_RESET BIT(2)
#define DMA_NORMAL 0U
#define DMA_LINKEDLIST BIT(4)
#define DMA_ADDR_ADJ_INCREMENT 1U
#define DMA_ADDR_ADJ_NO_CHANGE 0U
#define STM32_DMA_HAL_OVERRIDE 1U
#define HAL_DMA_STATE_RESET 0U
#define HAL_DMA_STATE_READY 1U
#define HAL_DMA_STATE_BUSY 2U
#define HAL_DMA_STATE_ERROR 3U
#define DMA_CSR_IDLEF BIT(0)
#define DMA_CLLR_LA 0xffffffc0U
#define DMA_CLBAR_LBA 0xfffffe00U
#define DMA_CLLR_UT1 BIT(0)
#define DMA_CLLR_UT2 BIT(1)
#define DMA_CLLR_UB1 BIT(2)
#define DMA_CLLR_USA BIT(3)
#define DMA_CLLR_UDA BIT(4)
#define DMA_CLLR_ULL BIT(5)
#define DMA_LSM_FULL_EXECUTION 1U
#define DMA_TCEM_EACH_LL_ITEM_TRANSFER 1U
#define DMA_LINKEDLIST_NORMAL 1U
#define DMA_LINK_ALLOCATED_PORT1 1U
#define DMA_CHANNEL_SEC 1U
#define DMA_CHANNEL_PRIV 2U
#define DMA_CHANNEL_SRC_SEC 4U
#define DMA_CHANNEL_DEST_SEC 8U
#define DMA_SRC_ALLOCATED_PORT0 1U
#define DMA_DEST_ALLOCATED_PORT1 2U
#define DMA_GPDMA_LINEAR_NODE 1U
#define SAI_xSR_OVRUDR BIT(0)
#define SAI_xSR_WCKCFG BIT(1)
#define SAI_xSR_AFSDET BIT(2)
#define SAI_xSR_LFSDET BIT(3)
#define SAI_xCR1_DMAEN BIT(0)
#define SAI_xCR1_SAIEN BIT(1)
#define SAI_ASYNCHRONOUS 0U
#define SAI_SYNCHRONOUS 1U
#define SAI_MODEMASTER_RX 0U
#define SAI_MODESLAVE_RX 1U
#define SAI_MODEMASTER_TX 2U
#define SAI_MODESLAVE_TX 3U
#define SAI_FIFOTHRESHOLD_HF 1U
#define SAI_FIFOTHRESHOLD_FULL 2U
#define SAI_AUDIO_FREQUENCY_384K 384000U
#define SAI_AUDIO_FREQUENCY_192K 192000U
#define SAI_AUDIO_FREQUENCY_96K 96000U
#define SAI_AUDIO_FREQUENCY_48K 48000U
#define SAI_AUDIO_FREQUENCY_44K 44100U
#define SAI_AUDIO_FREQUENCY_32K 32000U
#define SAI_AUDIO_FREQUENCY_22K 22050U
#define SAI_AUDIO_FREQUENCY_16K 16000U
#define SAI_AUDIO_FREQUENCY_11K 11025U
#define SAI_AUDIO_FREQUENCY_8K 8000U
#define SAI_MASTERDIVIDER_DISABLE 0U
#define SAI_MASTERDIVIDER_ENABLE 1U
#define SAI_PROTOCOL_DATASIZE_16BIT 16U
#define SAI_PROTOCOL_DATASIZE_24BIT 24U
#define SAI_PROTOCOL_DATASIZE_32BIT 32U
#define SAI_MONOMODE 1U
#define SAI_STEREOMODE 2U
#define SAI_I2S_STANDARD 0U
#define SAI_I2S_MSBJUSTIFIED 1U
#define SAI_I2S_LSBJUSTIFIED 2U
#define SAI_PCM_SHORT 3U
#define SAI_PCM_LONG 4U
#define MCLK_NO_DIV 0U
#define MCLK_DIV_256 1U

enum mclk_divider {
	FIXTURE_MCLK_NO_DIV = MCLK_NO_DIV,
	FIXTURE_MCLK_DIV_256 = MCLK_DIV_256,
};
#define SAI_FLAG_OVRUDR BIT(0)
#define SAI_FLAG_WCKCFG BIT(1)
#define SAI_FLAG_AFSDET BIT(2)
#define SAI_FLAG_LFSDET BIT(3)
#define HAL_OK 0
#define HAL_ERROR 1
#define HAL_SAI_ERROR_NONE 0U
#define HAL_SAI_STATE_READY 0U
#define HAL_SAI_STATE_BUSY_RX 1U
#define QUEUE_TYPE_STATIC 1U
#define HAL_DMA_QUEUE_STATE_READY 1U

#ifndef BUILD_ASSERT
#define BUILD_ASSERT(expr, ...) _Static_assert((expr), #expr)
#endif
#define __DSB() do { } while (false)
#define ARG_UNUSED(x) (void)(x)
#define LOG_ERR(...) do { } while (false)
#define LOG_DBG(...) do { } while (false)
#define LOG_WRN(...) do { } while (false)
#define __HAL_DMA_GET_FLAG(hdma, mask) ((hdma)->flags & (mask))
#define __HAL_DMA_CLEAR_FLAG(hdma, mask) ((hdma)->flags &= ~(mask))
#define __HAL_DMA_DISABLE(hdma) ((hdma)->Instance->CCR |= (DMA_CCR_SUSP | DMA_CCR_RESET))
#define __HAL_DMA_DISABLE_IT(hdma, mask) do { ARG_UNUSED(hdma); ARG_UNUSED(mask); } while (false)
#define __HAL_DMA_ENABLE_IT(hdma, mask) do { ARG_UNUSED(hdma); ARG_UNUSED(mask); } while (false)
#define __HAL_LINKDMA(hsai, field, hdma) do { (hsai)->field = &(hdma); (hdma).Parent = (hsai); } while (false)
#define __HAL_SAI_CLEAR_FLAG(hsai, mask) do { ARG_UNUSED(hsai); ARG_UNUSED(mask); } while (false)
#define __HAL_SAI_ENABLE(hsai) ((hsai)->Instance->CR1 |= SAI_xCR1_SAIEN)
#define __HAL_SAI_DISABLE(hsai) ((hsai)->Instance->CR1 &= ~SAI_xCR1_SAIEN)

typedef struct {
	uint32_t CCR;
	/* Keep host pointers intact; the target register is 32-bit on STM32N6. */
	uintptr_t CDAR;
	uint32_t CSR;
} DMA_Channel_TypeDef;

typedef struct {
	uint32_t unused;
} DMA_TypeDef;

#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"

typedef struct {
	uint32_t Priority;
	uint32_t TransferEventMode;
	uint32_t TransferAllocatedPort;
	uint32_t Mode;
} DMA_InitTypeDef;

typedef struct {
	uint32_t Priority;
	uint32_t LinkStepMode;
	uint32_t TransferEventMode;
	uint32_t LinkedListMode;
	uint32_t LinkAllocatedPort;
} DMA_InitLinkedListTypeDef;

typedef struct DMA_NodeTypeDef {
	uint32_t LinkRegisters[6];
} DMA_NodeTypeDef;

typedef struct {
	uint32_t NodeType;
	DMA_InitTypeDef Init;
	uint32_t SrcSecure;
	uint32_t DestSecure;
	uint32_t SrcAddress;
	uint32_t DstAddress;
	uint32_t DataSize;
} DMA_NodeConfTypeDef;

typedef struct {
	uint32_t Type;
	uint32_t State;
	uint32_t NodeNumber;
	DMA_NodeTypeDef *Head;
	DMA_NodeTypeDef *FirstCircularNode;
} DMA_QListTypeDef;

typedef struct DMA_HandleTypeDef {
	DMA_Channel_TypeDef *Instance;
	DMA_InitTypeDef Init;
	uint32_t Mode;
	uint32_t State;
	DMA_QListTypeDef *LinkedListQueue;
	DMA_InitLinkedListTypeDef InitLinkedList;
	void *Parent;
	uint32_t flags;
	void (*XferHalfCpltCallback)(void);
	void (*XferCpltCallback)(void);
	void (*XferErrorCallback)(void);
	void (*XferSuspendCallback)(void);
} DMA_HandleTypeDef;

typedef struct {
	uint32_t CR1;
	uint32_t SR;
	uint32_t DR;
} SAI_TypeDef;

typedef struct {
	uint32_t AudioMode;
	uint32_t Synchro;
	uint32_t FIFOThreshold;
	uint32_t AudioFrequency;
	uint32_t NoDivider;
	uint32_t MonoStereoMode;
} SAI_InitTypeDef;

typedef struct {
	SAI_TypeDef *Instance;
	SAI_InitTypeDef Init;
	DMA_HandleTypeDef *hdmatx;
	DMA_HandleTypeDef *hdmarx;
	uint32_t State;
	uint32_t ErrorCode;
	void *pBuffPtr;
	uint32_t XferSize;
	uint32_t XferCount;
} SAI_HandleTypeDef;

struct queue_item {
	void *buffer;
	size_t size;
};

struct sai_continuous_node_storage {
	DMA_NodeTypeDef node;
	uint32_t padding[7];
} __aligned(32);

struct stream {
	DMA_TypeDef *reg;
	const struct device *dma_dev;
	uint32_t dma_channel;
	struct dma_config dma_cfg;
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
};

struct stm32_sai_sub_data {
	SAI_HandleTypeDef hsai;
	DMA_HandleTypeDef hdma;
	struct stream stream;
	struct queue_item queue_buffer[CONFIG_I2S_STM32_SAI_BLOCK_COUNT];
	struct sai_continuous_node_storage *continuous_node_storage;
};

struct stm32_sai_sub_cfg {
	void *pincfg;
	bool mclk_enable;
	enum mclk_divider mclk_div;
	bool synchronous;
	enum i2s_dir dir;
	const struct device *controller;
};

static DMA_Channel_TypeDef fixture_dma_registers;
static SAI_TypeDef fixture_sai_registers;
static bool fixture_deinit_fails;
static bool fixture_list_init_fails;
static bool fixture_list_start_fails;
static bool fixture_list_start_fails_after_enable;
static unsigned int fixture_deinit_count;
static unsigned int fixture_list_start_count;
static unsigned int fixture_normal_start_count;
static bool fixture_sai_rx_used_linked_list;
static void *fixture_normal_start_destination;
static unsigned int fixture_sai_rx_linked_list_count;
static unsigned int fixture_sai_rx_stale_queue_start_count;
static struct sai_continuous_node_storage fixture_node_storage[SAI_CONT_RX_NODE_COUNT]
	__aligned(512);

static inline int HAL_SAI_DMAStop(SAI_HandleTypeDef *hsai)
{
	fixture_dma_registers.CCR &= ~DMA_CCR_EN;
	hsai->Instance->CR1 &= ~(SAI_xCR1_DMAEN | SAI_xCR1_SAIEN);
	fixture_dma_registers.CSR |= DMA_CSR_IDLEF;
	hsai->State = HAL_SAI_STATE_READY;
	hsai->hdmarx->State = HAL_DMA_STATE_READY;
	return HAL_OK;
}

static inline int HAL_DMAEx_List_DeInit(DMA_HandleTypeDef *hdma)
{
	fixture_deinit_count++;
	if (fixture_deinit_fails) {
		return HAL_ERROR;
	}
	hdma->Mode = DMA_NORMAL;
	hdma->State = HAL_DMA_STATE_RESET;
	hdma->LinkedListQueue = NULL;
	hdma->Parent = NULL;
	hdma->Instance->CSR |= DMA_CSR_IDLEF;
	return HAL_OK;
}

static inline int HAL_DMAEx_List_Init(DMA_HandleTypeDef *hdma)
{
	if (fixture_list_init_fails) {
		hdma->State = HAL_DMA_STATE_ERROR;
		hdma->Instance->CCR |= DMA_CCR_EN;
		return HAL_ERROR;
	}
	hdma->Mode = DMA_LINKEDLIST;
	hdma->State = HAL_DMA_STATE_READY;
	hdma->Instance->CCR |= DMA_CCR_LSM;
	return HAL_OK;
}

static inline int HAL_DMA_ConfigChannelAttributes(DMA_HandleTypeDef *hdma, uint32_t attributes)
{
	ARG_UNUSED(hdma);
	ARG_UNUSED(attributes);
	return HAL_OK;
}

static inline int HAL_DMAEx_List_BuildNode(const DMA_NodeConfTypeDef *conf, DMA_NodeTypeDef *node)
{
	memset(node, 0, sizeof(*node));
	node->LinkRegisters[4] = conf->DstAddress;
	return HAL_OK;
}

static inline int HAL_DMAEx_List_LinkQ(DMA_HandleTypeDef *hdma, DMA_QListTypeDef *queue)
{
	hdma->LinkedListQueue = queue;
	return HAL_OK;
}

static inline int HAL_DMAEx_List_Start_IT(DMA_HandleTypeDef *hdma)
{
	fixture_list_start_count++;
	if (fixture_list_start_fails) {
		return HAL_ERROR;
	}
	if (hdma->LinkedListQueue == NULL ||
	    (hdma->State != HAL_DMA_STATE_READY &&
	     !(hdma->State == HAL_DMA_STATE_BUSY &&
	       (hdma->Instance->CCR & DMA_CCR_LSM) != 0U))) {
		return HAL_ERROR;
	}
	if (hdma->State == HAL_DMA_STATE_READY) {
		hdma->State = HAL_DMA_STATE_BUSY;
	}
	hdma->Instance->CCR |= DMA_CCR_EN;
	hdma->Instance->CCR |= DMA_CCR_LSM;
	hdma->Instance->CSR &= ~DMA_CSR_IDLEF;
	if (fixture_list_start_fails_after_enable) {
		return HAL_ERROR;
	}
	return HAL_OK;
}

static inline void HAL_DMA_IRQHandler(DMA_HandleTypeDef *hdma)
{
	ARG_UNUSED(hdma);
}

static inline int HAL_SAI_Transmit_DMA(SAI_HandleTypeDef *hsai, void *buffer, uint32_t size)
{
	ARG_UNUSED(hsai);
	ARG_UNUSED(buffer);
	ARG_UNUSED(size);
	return HAL_OK;
}

static inline int HAL_SAI_Receive_DMA(SAI_HandleTypeDef *hsai, void *buffer, uint32_t size)
{
	if (hsai->State != HAL_SAI_STATE_READY || buffer == NULL || size == 0U) {
		return HAL_ERROR;
	}
	if ((hsai->hdmarx->Mode & DMA_LINKEDLIST) == DMA_LINKEDLIST) {
		fixture_sai_rx_used_linked_list = true;
		fixture_sai_rx_linked_list_count++;
		if (hsai->hdmarx->LinkedListQueue == NULL) {
			return HAL_ERROR;
		}
		if (hsai->hdmarx->State == HAL_DMA_STATE_BUSY &&
		    (hsai->hdmarx->Instance->CCR & DMA_CCR_LSM) != 0U) {
			fixture_sai_rx_stale_queue_start_count++;
		}
		hsai->hdmarx->LinkedListQueue->Head->LinkRegisters[4] =
			(uint32_t)(uintptr_t)buffer;
		if (HAL_DMAEx_List_Start_IT(hsai->hdmarx) != HAL_OK) {
			return HAL_ERROR;
		}
		hsai->State = HAL_SAI_STATE_BUSY_RX;
		return HAL_OK;
	}
	fixture_normal_start_count++;
	fixture_normal_start_destination = buffer;
	if (hsai->hdmarx->State != HAL_DMA_STATE_READY) {
		return HAL_ERROR;
	}
	hsai->hdmarx->State = HAL_DMA_STATE_BUSY;
	hsai->hdmarx->Instance->CCR |= DMA_CCR_EN;
	hsai->State = HAL_SAI_STATE_BUSY_RX;
	return HAL_OK;
}

static inline int HAL_DMA_Init(DMA_HandleTypeDef *hdma)
{
	hdma->Mode = hdma->Init.Mode;
	hdma->State = HAL_DMA_STATE_READY;
	hdma->LinkedListQueue = NULL;
	hdma->Instance->CCR &= ~DMA_CCR_LSM;
	return HAL_OK;
}

static inline int dma_stm32_zcfg_to_halcfg(const struct device *dev,
					    const struct dma_config *config,
					    DMA_InitTypeDef *hal_config,
					    uint16_t source_adjust,
					    uint16_t destination_adjust)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(config);
	ARG_UNUSED(source_adjust);
	ARG_UNUSED(destination_adjust);
	hal_config->Mode = DMA_NORMAL;
	return 0;
}

static inline DMA_Channel_TypeDef *fixture_dma_get_instance(DMA_TypeDef *reg,
							      uint32_t channel)
{
	ARG_UNUSED(reg);
	ARG_UNUSED(channel);
	return &fixture_dma_registers;
}

#define STM32_DMA_GET_INSTANCE(reg, channel) fixture_dma_get_instance((reg), (channel))

static inline int HAL_SAI_InitProtocol(SAI_HandleTypeDef *hsai, uint32_t protocol,
					      uint32_t word_size, uint32_t slots)
{
	ARG_UNUSED(protocol);
	ARG_UNUSED(word_size);
	ARG_UNUSED(slots);
	hsai->State = HAL_SAI_STATE_READY;
	return HAL_OK;
}

static inline uint32_t stm32_reg_read(volatile uint32_t *reg)
{
	return *reg;
}

static inline void stm32_reg_write(volatile uint32_t *reg, uint32_t value)
{
	*reg = value;
}

static inline void sys_cache_data_flush_range(void *address, size_t size)
{
	ARG_UNUSED(address);
	ARG_UNUSED(size);
}

#endif
