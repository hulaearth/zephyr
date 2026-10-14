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

#define CONFIG_I2S_STM32_SAI_CONTINUOUS_RX_N6 1
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
#define __HAL_DMA_GET_FLAG(hdma, mask) ((hdma)->flags & (mask))
#define __HAL_DMA_CLEAR_FLAG(hdma, mask) ((hdma)->flags &= ~(mask))
#define __HAL_DMA_DISABLE_IT(hdma, mask) do { ARG_UNUSED(hdma); ARG_UNUSED(mask); } while (false)
#define __HAL_DMA_ENABLE_IT(hdma, mask) do { ARG_UNUSED(hdma); ARG_UNUSED(mask); } while (false)
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
	SAI_TypeDef *Instance;
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
	enum i2s_dir dir;
};

static DMA_Channel_TypeDef fixture_dma_registers;
static SAI_TypeDef fixture_sai_registers;
static bool fixture_deinit_fails;
static unsigned int fixture_deinit_count;
static struct sai_continuous_node_storage fixture_node_storage[SAI_CONT_RX_NODE_COUNT]
	__aligned(512);

static inline int HAL_SAI_DMAStop(SAI_HandleTypeDef *hsai)
{
	fixture_dma_registers.CCR &= ~DMA_CCR_EN;
	hsai->Instance->CR1 &= ~(SAI_xCR1_DMAEN | SAI_xCR1_SAIEN);
	fixture_dma_registers.CSR |= DMA_CSR_IDLEF;
	hsai->State = HAL_SAI_STATE_READY;
	return HAL_OK;
}

static inline int HAL_DMAEx_List_DeInit(DMA_HandleTypeDef *hdma)
{
	fixture_deinit_count++;
	if (fixture_deinit_fails) {
		return HAL_ERROR;
	}
	hdma->Instance->CSR |= DMA_CSR_IDLEF;
	return HAL_OK;
}

static inline int HAL_DMAEx_List_Init(DMA_HandleTypeDef *hdma)
{
	ARG_UNUSED(hdma);
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
	ARG_UNUSED(conf);
	memset(node, 0, sizeof(*node));
	return HAL_OK;
}

static inline int HAL_DMAEx_List_LinkQ(DMA_HandleTypeDef *hdma, DMA_QListTypeDef *queue)
{
	ARG_UNUSED(hdma);
	ARG_UNUSED(queue);
	return HAL_OK;
}

static inline int HAL_DMAEx_List_Start_IT(DMA_HandleTypeDef *hdma)
{
	hdma->Instance->CCR |= DMA_CCR_EN;
	hdma->Instance->CSR &= ~DMA_CSR_IDLEF;
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
	ARG_UNUSED(hsai);
	ARG_UNUSED(buffer);
	ARG_UNUSED(size);
	return HAL_OK;
}

static inline void sys_cache_data_flush_range(void *address, size_t size)
{
	ARG_UNUSED(address);
	ARG_UNUSED(size);
}

#endif
