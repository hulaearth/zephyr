/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Hula Earth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/cache.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include <zephyr/sys/util.h>
#include <zephyr/pm/stm32n6_retained_stop.h>

#include <stm32_ll_bus.h>
#include <stm32_ll_cortex.h>
#include <stm32_ll_exti.h>
#include <stm32_ll_pwr.h>
#include <stm32_ll_rcc.h>
#include <stm32n6xx_hal_rcc.h>

BUILD_ASSERT(DT_SAME_NODE(DT_CHOSEN(zephyr_system_timer), DT_NODELABEL(lptim1)),
	     "Retained Stop requires LPTIM1 as the system timer");
BUILD_ASSERT(IS_ENABLED(CONFIG_STM32_LPTIM_TIMER) && IS_ENABLED(CONFIG_STM32_LPTIM_CLOCK_LSE),
	     "Retained Stop requires LPTIM/LSE system timer");
BUILD_ASSERT(!IS_ENABLED(CONFIG_PM_STATE_SET_IRQ_UNLOCKED),
	     "Retained Stop requires IRQ-locked PM hooks");
BUILD_ASSERT(!IS_ENABLED(CONFIG_MEMC) && !IS_ENABLED(CONFIG_MSPI),
	     "Retained Stop requires boot-inherited XSPI ownership");

#define RETAINED_STOP_POLL_LIMIT          100000U
#define RETAINED_STOP_VECTOR_COUNT        (16U + CONFIG_NUM_IRQS)
#define RETAINED_STOP_VECTOR_ALIGN        BIT(LOG2CEIL(4U * RETAINED_STOP_VECTOR_COUNT))
#define RETAINED_STOP_XSPI_CANARY_WORDS   8U
#define RETAINED_STOP_XSPI_CANARY_SIZE    (RETAINED_STOP_XSPI_CANARY_WORDS * sizeof(uint32_t))
#define RETAINED_STOP_XSPI_STABLE_CR_MASK (~(XSPI_CR_FMODE | XSPI_CR_ABORT))

static uintptr_t retained_stop_vectors[RETAINED_STOP_VECTOR_COUNT]
	__aligned(RETAINED_STOP_VECTOR_ALIGN);

struct retained_xspi_state {
	uint32_t cr;
	uint32_t dcr1;
	uint32_t dcr2;
	uint32_t dcr3;
	uint32_t dcr4;
	uint32_t ccr;
	uint32_t tcr;
	uint32_t ir;
	uint32_t abr;
	uint32_t wccr;
	uint32_t wtcr;
	uint32_t wir;
	uint32_t wabr;
	uint32_t hlcr;
};

static struct retained_xspi_state retained_xspi;
static uint32_t retained_xspi_canary[RETAINED_STOP_XSPI_CANARY_WORDS];

static volatile bool stop_enabled;
static volatile struct stm32n6_retained_stop_stats stop_stats;

static inline void saturating_increment(volatile uint32_t *value)
{
	if (*value != UINT32_MAX) {
		++*value;
	}
}

int stm32n6_retained_stop_set_enabled(bool enabled)
{
	unsigned int key = irq_lock();

	if (enabled) {
		stop_stats.last_rejection = STM32N6_RETAINED_STOP_REJECTION_NONE;
	}
	stop_enabled = enabled;
	irq_unlock(key);
	return 0;
}

void stm32n6_retained_stop_get_stats(struct stm32n6_retained_stop_stats *stats)
{
	if (stats == NULL) {
		return;
	}

	unsigned int key = irq_lock();

	stats->attempts = stop_stats.attempts;
	stats->entries = stop_stats.entries;
	stats->wakes = stop_stats.wakes;
	stats->rejections = stop_stats.rejections;
	stats->last_rejection = stop_stats.last_rejection;
	irq_unlock(key);
}

static bool gpdma_channel_enabled(void)
{
#define GPDMA_CHANNEL_ENABLED(channel) (((channel)->CCR & DMA_CCR_EN) != 0U)
	return GPDMA_CHANNEL_ENABLED(GPDMA1_Channel0) || GPDMA_CHANNEL_ENABLED(GPDMA1_Channel1) ||
	       GPDMA_CHANNEL_ENABLED(GPDMA1_Channel2) || GPDMA_CHANNEL_ENABLED(GPDMA1_Channel3) ||
	       GPDMA_CHANNEL_ENABLED(GPDMA1_Channel4) || GPDMA_CHANNEL_ENABLED(GPDMA1_Channel5) ||
	       GPDMA_CHANNEL_ENABLED(GPDMA1_Channel6) || GPDMA_CHANNEL_ENABLED(GPDMA1_Channel7) ||
	       GPDMA_CHANNEL_ENABLED(GPDMA1_Channel8) || GPDMA_CHANNEL_ENABLED(GPDMA1_Channel9) ||
	       GPDMA_CHANNEL_ENABLED(GPDMA1_Channel10) || GPDMA_CHANNEL_ENABLED(GPDMA1_Channel11) ||
	       GPDMA_CHANNEL_ENABLED(GPDMA1_Channel12) || GPDMA_CHANNEL_ENABLED(GPDMA1_Channel13) ||
	       GPDMA_CHANNEL_ENABLED(GPDMA1_Channel14) || GPDMA_CHANNEL_ENABLED(GPDMA1_Channel15);
#undef GPDMA_CHANNEL_ENABLED
}

static void snapshot_xspi_configuration(void)
{
	retained_xspi.cr = XSPI1->CR;
	retained_xspi.dcr1 = XSPI1->DCR1;
	retained_xspi.dcr2 = XSPI1->DCR2;
	retained_xspi.dcr3 = XSPI1->DCR3;
	retained_xspi.dcr4 = XSPI1->DCR4;
	retained_xspi.ccr = XSPI1->CCR;
	retained_xspi.tcr = XSPI1->TCR;
	retained_xspi.ir = XSPI1->IR;
	retained_xspi.abr = XSPI1->ABR;
	retained_xspi.wccr = XSPI1->WCCR;
	retained_xspi.wtcr = XSPI1->WTCR;
	retained_xspi.wir = XSPI1->WIR;
	retained_xspi.wabr = XSPI1->WABR;
	retained_xspi.hlcr = XSPI1->HLCR;
}

static bool xspi_configuration_matches(void)
{
	return (XSPI1->CR & RETAINED_STOP_XSPI_STABLE_CR_MASK) ==
		       (retained_xspi.cr & RETAINED_STOP_XSPI_STABLE_CR_MASK) &&
	       XSPI1->DCR1 == retained_xspi.dcr1 && XSPI1->DCR2 == retained_xspi.dcr2 &&
	       XSPI1->DCR3 == retained_xspi.dcr3 && XSPI1->DCR4 == retained_xspi.dcr4 &&
	       XSPI1->CCR == retained_xspi.ccr && XSPI1->TCR == retained_xspi.tcr &&
	       XSPI1->IR == retained_xspi.ir && XSPI1->ABR == retained_xspi.abr &&
	       XSPI1->WCCR == retained_xspi.wccr && XSPI1->WTCR == retained_xspi.wtcr &&
	       XSPI1->WIR == retained_xspi.wir && XSPI1->WABR == retained_xspi.wabr &&
	       XSPI1->HLCR == retained_xspi.hlcr;
}

static bool prepare_xspi_for_stop(void)
{
	volatile uint32_t *mapped = (volatile uint32_t *)(uintptr_t)XSPI1_BASE;
	uint32_t remaining;

#if defined(CONFIG_CACHE_MANAGEMENT) && defined(CONFIG_DCACHE)
	if (sys_cache_data_flush_all() != 0 ||
	    sys_cache_data_flush_and_invd_range((void *)(uintptr_t)XSPI1_BASE,
						RETAINED_STOP_XSPI_CANARY_SIZE) != 0) {
		return false;
	}
#endif
	for (uint32_t i = 0U; i < RETAINED_STOP_XSPI_CANARY_WORDS; ++i) {
		retained_xspi_canary[i] = mapped[i];
	}
	__DSB();
	__ISB();

	XSPI1->CR |= XSPI_CR_ABORT;
	for (remaining = RETAINED_STOP_POLL_LIMIT; remaining != 0U; --remaining) {
		if ((XSPI1->SR & XSPI_SR_TCF) != 0U) {
			break;
		}
	}
	if (remaining == 0U) {
		return false;
	}
	XSPI1->FCR = XSPI_FCR_CTCF;
	for (remaining = RETAINED_STOP_POLL_LIMIT; remaining != 0U; --remaining) {
		if ((XSPI1->SR & XSPI_SR_BUSY) == 0U) {
			break;
		}
	}
	if (remaining == 0U || (XSPI1->CR & XSPI_CR_ABORT) != 0U) {
		return false;
	}

	XSPI1->CR &= ~XSPI_CR_FMODE;
	__DSB();
	__ISB();
	return (XSPI1->CR & XSPI_CR_FMODE) == 0U && xspi_configuration_matches();
}

static bool restore_xspi_mapping_and_verify(void)
{
	volatile uint32_t *mapped = (volatile uint32_t *)(uintptr_t)XSPI1_BASE;

	if ((XSPI1->CR & XSPI_CR_EN) == 0U ||
	    (XSPI1->CR & RETAINED_STOP_XSPI_STABLE_CR_MASK) !=
		    (retained_xspi.cr & RETAINED_STOP_XSPI_STABLE_CR_MASK)) {
		return false;
	}
	XSPI1->CR = (XSPI1->CR & ~XSPI_CR_FMODE) | (retained_xspi.cr & XSPI_CR_FMODE);
	__DSB();
	__ISB();
	if ((XSPI1->CR & XSPI_CR_FMODE) != (retained_xspi.cr & XSPI_CR_FMODE) ||
	    !xspi_configuration_matches()) {
		return false;
	}

#if defined(CONFIG_CACHE_MANAGEMENT) && defined(CONFIG_DCACHE)
	if (sys_cache_data_invd_range((void *)(uintptr_t)XSPI1_BASE,
				      RETAINED_STOP_XSPI_CANARY_SIZE) != 0) {
		return false;
	}
#endif
	for (uint32_t i = 0U; i < RETAINED_STOP_XSPI_CANARY_WORDS; ++i) {
		if (mapped[i] != retained_xspi_canary[i]) {
			return false;
		}
	}
	return true;
}

struct lptim_wake_gates {
	bool apb_low_power;
	bool exti_interrupt;
};

static bool prepare_lptim_wake_gates(struct lptim_wake_gates *saved)
{
	saved->apb_low_power =
		LL_APB1_GRP1_IsEnabledClockLowPower(LL_APB1_GRP1_PERIPH_LPTIM1) == 1U;
	saved->exti_interrupt = LL_EXTI_IsEnabledIT_32_63(LL_EXTI_LINE_52) == 1U;
	LL_APB1_GRP1_EnableClockLowPower(LL_APB1_GRP1_PERIPH_LPTIM1);
	LL_EXTI_EnableIT_32_63(LL_EXTI_LINE_52);
	return LL_APB1_GRP1_IsEnabledClockLowPower(LL_APB1_GRP1_PERIPH_LPTIM1) == 1U &&
	       LL_EXTI_IsEnabledIT_32_63(LL_EXTI_LINE_52) == 1U;
}

static bool restore_lptim_wake_gates(const struct lptim_wake_gates *saved)
{
	if (!saved->apb_low_power) {
		LL_APB1_GRP1_DisableClockLowPower(LL_APB1_GRP1_PERIPH_LPTIM1);
	}
	if (!saved->exti_interrupt) {
		LL_EXTI_DisableIT_32_63(LL_EXTI_LINE_52);
	}
	return (LL_APB1_GRP1_IsEnabledClockLowPower(LL_APB1_GRP1_PERIPH_LPTIM1) == 1U) ==
		       saved->apb_low_power &&
	       (LL_EXTI_IsEnabledIT_32_63(LL_EXTI_LINE_52) == 1U) == saved->exti_interrupt;
}

struct stop_register_state {
	uint32_t scr;
	uint32_t power_down_mode;
	bool pwr_clock;
	bool bsec_clock;
	bool bsec_sleep_clock;
	bool hsi_in_stop;
	bool hse_enabled;
	uint32_t stop_voltage_scale;
};

#define STOP_SCR_MASK (SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SLEEPONEXIT_Msk | SCB_SCR_SEVONPEND_Msk)

static void snapshot_stop_register_state(struct stop_register_state *saved)
{
	saved->scr = SCB->SCR & STOP_SCR_MASK;
	saved->pwr_clock = __HAL_RCC_PWR_IS_CLK_ENABLED() != 0U;
	__HAL_RCC_PWR_CLK_ENABLE();
	saved->power_down_mode = LL_PWR_GetPowerDownModeDS();
	saved->bsec_clock = __HAL_RCC_BSEC_IS_CLK_ENABLED() != 0U;
	saved->bsec_sleep_clock = __HAL_RCC_BSEC_IS_CLK_SLEEP_ENABLED() != 0U;
	saved->hsi_in_stop = LL_RCC_HSI_IsEnabledInStopMode() != 0U;
	saved->hse_enabled = (RCC->CSR & RCC_CSR_HSEONS) != 0U;
	saved->stop_voltage_scale = LL_PWR_GetStopModeRegulVoltageScaling();
}

static bool restore_stop_register_state(const struct stop_register_state *saved)
{
	if (saved->hsi_in_stop) {
		LL_RCC_HSI_EnableInStopMode();
	} else {
		LL_RCC_HSI_DisableInStopMode();
	}
	LL_PWR_SetStopModeRegulVoltageScaling(saved->stop_voltage_scale);
	LL_PWR_SetPowerDownModeDS(saved->power_down_mode);
	if (!saved->bsec_clock) {
		__HAL_RCC_BSEC_CLK_DISABLE();
	}
	if (!saved->bsec_sleep_clock) {
		__HAL_RCC_BSEC_CLK_SLEEP_DISABLE();
	}
	SCB->SCR = (SCB->SCR & ~STOP_SCR_MASK) | saved->scr;
	if (LL_PWR_GetPowerDownModeDS() != saved->power_down_mode ||
	    LL_PWR_GetStopModeRegulVoltageScaling() != saved->stop_voltage_scale ||
	    (LL_RCC_HSI_IsEnabledInStopMode() != 0U) != saved->hsi_in_stop ||
	    (__HAL_RCC_BSEC_IS_CLK_ENABLED() != 0U) != saved->bsec_clock ||
	    (__HAL_RCC_BSEC_IS_CLK_SLEEP_ENABLED() != 0U) != saved->bsec_sleep_clock ||
	    (SCB->SCR & STOP_SCR_MASK) != saved->scr) {
		return false;
	}
	if (saved->pwr_clock) {
		return __HAL_RCC_PWR_IS_CLK_ENABLED() != 0U;
	}
	__HAL_RCC_PWR_CLK_DISABLE();
	return __HAL_RCC_PWR_IS_CLK_ENABLED() == 0U;
}

static bool restore_hse_state(bool enabled)
{
	uint32_t remaining;
	bool currently_enabled = (RCC->CSR & RCC_CSR_HSEONS) != 0U;

	if (currently_enabled != enabled) {
		if (enabled) {
			LL_RCC_HSE_Enable();
		} else {
			LL_RCC_HSE_Disable();
		}
	}
	for (remaining = RETAINED_STOP_POLL_LIMIT; remaining != 0U; --remaining) {
		if ((LL_RCC_HSE_IsReady() != 0U) == enabled) {
			break;
		}
	}
	return remaining != 0U && ((RCC->CSR & RCC_CSR_HSEONS) != 0U) == enabled &&
	       (LL_RCC_HSE_IsReady() != 0U) == enabled;
}

static bool retained_profile_ready(void)
{
	return LL_RCC_HSI_IsReady() == 1U && LL_RCC_HSI_GetDivider() == LL_RCC_HSI_DIV_1 &&
	       LL_RCC_GetCpuClkSource() == LL_RCC_CPU_CLKSOURCE_STATUS_HSI &&
	       LL_RCC_GetSysClkSource() == LL_RCC_SYS_CLKSOURCE_STATUS_HSI &&
	       LL_RCC_PLL1_IsReady() == 0U && LL_RCC_PLL2_IsReady() == 0U &&
	       LL_RCC_PLL3_IsReady() == 0U && LL_RCC_PLL4_IsReady() == 0U &&
	       LL_RCC_GetCLKPClockSource(LL_RCC_CLKP_CLKSOURCE) == LL_RCC_CLKP_CLKSOURCE_HSI &&
	       LL_RCC_GetXSPIClockSource(LL_RCC_XSPI1_CLKSOURCE) == LL_RCC_XSPI1_CLKSOURCE_CLKP &&
	       LL_AHB5_GRP1_IsEnabledClock(LL_AHB5_GRP1_PERIPH_XSPI1) == 1U &&
	       LL_AHB5_GRP1_IsEnabledClock(LL_AHB5_GRP1_PERIPH_XSPIM) == 1U &&
	       LL_AHB5_GRP1_IsEnabledClockLowPower(LL_AHB5_GRP1_PERIPH_XSPI1) == 1U &&
	       LL_AHB5_GRP1_IsEnabledClockLowPower(LL_AHB5_GRP1_PERIPH_XSPIM) == 1U &&
	       LL_MEM_IsEnabledClock(LL_MEM_AXISRAM5) == 0U &&
	       LL_AHB5_GRP1_IsEnabledClock(LL_AHB5_GRP1_PERIPH_NPU) == 0U &&
	       (SAI2_Block_A->CR1 & (SAI_xCR1_SAIEN | SAI_xCR1_DMAEN)) == 0U &&
	       (SAI2_Block_B->CR1 & (SAI_xCR1_SAIEN | SAI_xCR1_DMAEN)) == 0U &&
	       (XSPI1->CR & XSPI_CR_EN) != 0U && (XSPI1->CR & XSPI_CR_FMODE) == XSPI_CR_FMODE;
}

static bool restore_retained_memory_clock_path(void)
{
	uint32_t remaining;

	if (LL_RCC_HSI_IsReady() == 0U) {
		LL_RCC_HSI_Enable();
		for (remaining = RETAINED_STOP_POLL_LIMIT; remaining != 0U; --remaining) {
			if (LL_RCC_HSI_IsReady() == 1U) {
				break;
			}
		}
		if (remaining == 0U) {
			return false;
		}
	}

	if (LL_RCC_GetCpuClkSource() != LL_RCC_CPU_CLKSOURCE_STATUS_HSI) {
		LL_RCC_SetCpuClkSource(LL_RCC_CPU_CLKSOURCE_HSI);
		for (remaining = RETAINED_STOP_POLL_LIMIT; remaining != 0U; --remaining) {
			if (LL_RCC_GetCpuClkSource() == LL_RCC_CPU_CLKSOURCE_STATUS_HSI) {
				break;
			}
		}
		if (remaining == 0U) {
			return false;
		}
	}

	if (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_HSI) {
		LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSI);
		for (remaining = RETAINED_STOP_POLL_LIMIT; remaining != 0U; --remaining) {
			if (LL_RCC_GetSysClkSource() == LL_RCC_SYS_CLKSOURCE_STATUS_HSI) {
				break;
			}
		}
		if (remaining == 0U) {
			return false;
		}
	}

	LL_RCC_SetCLKPClockSource(LL_RCC_CLKP_CLKSOURCE_HSI);
	LL_RCC_SetXSPIClockSource(LL_RCC_XSPI1_CLKSOURCE_CLKP);
	LL_AHB5_GRP1_EnableClock(LL_AHB5_GRP1_PERIPH_XSPIM);
	LL_AHB5_GRP1_EnableClock(LL_AHB5_GRP1_PERIPH_XSPI1);
	LL_AHB5_GRP1_EnableClockLowPower(LL_AHB5_GRP1_PERIPH_XSPIM);
	LL_AHB5_GRP1_EnableClockLowPower(LL_AHB5_GRP1_PERIPH_XSPI1);

	return LL_RCC_HSI_IsReady() == 1U &&
	       LL_RCC_GetCpuClkSource() == LL_RCC_CPU_CLKSOURCE_STATUS_HSI &&
	       LL_RCC_GetSysClkSource() == LL_RCC_SYS_CLKSOURCE_STATUS_HSI &&
	       LL_RCC_GetCLKPClockSource(LL_RCC_CLKP_CLKSOURCE) == LL_RCC_CLKP_CLKSOURCE_HSI &&
	       LL_RCC_GetXSPIClockSource(LL_RCC_XSPI1_CLKSOURCE) == LL_RCC_XSPI1_CLKSOURCE_CLKP &&
	       LL_AHB5_GRP1_IsEnabledClock(LL_AHB5_GRP1_PERIPH_XSPIM) == 1U &&
	       LL_AHB5_GRP1_IsEnabledClock(LL_AHB5_GRP1_PERIPH_XSPI1) == 1U &&
	       LL_AHB5_GRP1_IsEnabledClockLowPower(LL_AHB5_GRP1_PERIPH_XSPIM) == 1U &&
	       LL_AHB5_GRP1_IsEnabledClockLowPower(LL_AHB5_GRP1_PERIPH_XSPI1) == 1U &&
	       (XSPI1->CR & XSPI_CR_EN) != 0U;
}

static __noreturn void retained_stop_fail_stop(void)
{
	__disable_irq();
	for (;;) {
		__WFI();
	}
}

static bool install_retained_stop_vectors(uint32_t *original_vtor)
{
	uintptr_t handler = (uintptr_t)retained_stop_fail_stop;

	if (__get_PRIMASK() == 0U) {
		return false;
	}

	*original_vtor = SCB->VTOR;
	/* Scalar stores keep vector/FPU context out of the idle Stop path. */
	volatile uint32_t *vectors = retained_stop_vectors;

	vectors[0] = (uintptr_t)__get_MSP();
	for (uint32_t i = 1U; i < RETAINED_STOP_VECTOR_COUNT; ++i) {
		vectors[i] = handler;
	}
#if defined(CONFIG_CACHE_MANAGEMENT) && defined(CONFIG_DCACHE)
	if (sys_cache_data_flush_range(retained_stop_vectors, sizeof(retained_stop_vectors)) != 0) {
		return false;
	}
#endif
	__DSB();
	__ISB();
	SCB->VTOR = (uint32_t)retained_stop_vectors;
	__DSB();
	__ISB();
	if (SCB->VTOR != (uint32_t)retained_stop_vectors) {
		SCB->VTOR = *original_vtor;
		__DSB();
		__ISB();
		return false;
	}
	return true;
}

static bool restore_original_vectors(uint32_t original_vtor)
{
	SCB->VTOR = original_vtor;
	__DSB();
	__ISB();
	return SCB->VTOR == original_vtor;
}

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	unsigned int key;
	bool eligible;
	bool have_saved_state = false;
	struct lptim_wake_gates lptim_gates;
	struct stop_register_state saved;

	saturating_increment(&stop_stats.attempts);
	saved.scr = SCB->SCR & STOP_SCR_MASK;
	key = arch_pm_state_set_prepare();
	const bool enabled = stop_enabled;

	eligible = enabled && state == PM_STATE_SUSPEND_TO_IDLE && substate_id == 1U;
	if (eligible) {
		snapshot_stop_register_state(&saved);
		have_saved_state = true;
		eligible = retained_profile_ready();
		if (!eligible) {
			stop_stats.last_rejection = STM32N6_RETAINED_STOP_REJECTION_PROFILE;
		} else if (gpdma_channel_enabled()) {
			eligible = false;
			stop_stats.last_rejection = STM32N6_RETAINED_STOP_REJECTION_DMA_ACTIVE;
		} else {
			snapshot_xspi_configuration();
		}
	} else if (enabled) {
		stop_stats.last_rejection = STM32N6_RETAINED_STOP_REJECTION_INELIGIBLE;
	}
	if (!eligible) {
		if (have_saved_state && !restore_stop_register_state(&saved)) {
			retained_stop_fail_stop();
		}
		saturating_increment(&stop_stats.rejections);
		LL_LPM_EnableSleep();
		__DSB();
		__ISB();
		__WFI();
		SCB->SCR = (SCB->SCR & ~STOP_SCR_MASK) | saved.scr;
		arch_pm_state_set_finish(key);
		return;
	}

	/* Save and temporarily open documented LPTIM1 Stop wake gates. */
	if (!prepare_lptim_wake_gates(&lptim_gates)) {
		stop_stats.last_rejection = STM32N6_RETAINED_STOP_REJECTION_WAKE_GATES;
		if (!restore_lptim_wake_gates(&lptim_gates) ||
		    !restore_stop_register_state(&saved)) {
			retained_stop_fail_stop();
		}
		saturating_increment(&stop_stats.rejections);
		LL_LPM_EnableSleep();
		__DSB();
		__ISB();
		__WFI();
		SCB->SCR = (SCB->SCR & ~STOP_SCR_MASK) | saved.scr;
		arch_pm_state_set_finish(key);
		return;
	}

	/* Prepare only the low-power gates needed by this strict Stop profile. */
	__HAL_RCC_BSEC_CLK_ENABLE();
	__HAL_RCC_BSEC_CLK_SLEEP_ENABLE();
	LL_RCC_HSI_EnableInStopMode();
	LL_PWR_SetStopModeRegulVoltageScaling(LL_PWR_REGU_STOP_VOLTAGE_SCALE3);
	LL_PWR_SetPowerDownModeDS(LL_PWR_POWERDOWN_MODE_DS_STOP);
	if (LL_PWR_GetPowerDownModeDS() != LL_PWR_POWERDOWN_MODE_DS_STOP ||
	    LL_PWR_GetStopModeRegulVoltageScaling() != LL_PWR_REGU_STOP_VOLTAGE_SCALE3 ||
	    LL_RCC_HSI_IsEnabledInStopMode() != 1U || !__HAL_RCC_BSEC_IS_CLK_ENABLED() ||
	    !__HAL_RCC_BSEC_IS_CLK_SLEEP_ENABLED()) {
		stop_stats.last_rejection = STM32N6_RETAINED_STOP_REJECTION_POWER_CONFIG;
		if (!restore_lptim_wake_gates(&lptim_gates) ||
		    !restore_stop_register_state(&saved)) {
			retained_stop_fail_stop();
		}
		saturating_increment(&stop_stats.rejections);
		LL_LPM_EnableSleep();
		__DSB();
		__ISB();
		__WFI();
		SCB->SCR = (SCB->SCR & ~STOP_SCR_MASK) | saved.scr;
		arch_pm_state_set_finish(key);
		return;
	}

	uint32_t original_vtor;

	if (!install_retained_stop_vectors(&original_vtor)) {
		retained_stop_fail_stop();
	}
	if (!prepare_xspi_for_stop()) {
		retained_stop_fail_stop();
	}
	LL_PWR_ClearFlag_STOP_SB();
	LL_LPM_DisableEventOnPend();
	LL_LPM_DisableSleepOnExit();
	LL_LPM_EnableDeepSleep();
	__DSB();
	__ISB();
	__WFI();
	if (LL_PWR_IsActiveFlag_STOP() == 0U) {
		stop_stats.last_rejection = STM32N6_RETAINED_STOP_REJECTION_STOP_FLAG;
		if (!restore_retained_memory_clock_path() || !restore_xspi_mapping_and_verify() ||
		    !restore_lptim_wake_gates(&lptim_gates) ||
		    !restore_stop_register_state(&saved) ||
		    !restore_original_vectors(original_vtor)) {
			retained_stop_fail_stop();
		}
		saturating_increment(&stop_stats.rejections);
		LL_LPM_EnableSleep();
		SCB->SCR = (SCB->SCR & ~STOP_SCR_MASK) | saved.scr;
		arch_pm_state_set_finish(key);
		return;
	}
	saturating_increment(&stop_stats.entries);
	stop_stats.last_rejection = STM32N6_RETAINED_STOP_REJECTION_NONE;

	/* Stay in the internal-SRAM relocation closure until memory is verified. */
	LL_LPM_EnableSleep();
	if (!restore_retained_memory_clock_path() || !restore_xspi_mapping_and_verify() ||
	    !restore_hse_state(saved.hse_enabled) || !restore_lptim_wake_gates(&lptim_gates) ||
	    !restore_stop_register_state(&saved) || !restore_original_vectors(original_vtor)) {
		retained_stop_fail_stop();
	}

	saturating_increment(&stop_stats.wakes);
	arch_pm_state_set_finish(key);
}
