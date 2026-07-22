/*
 * Copyright (c) 2025 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef STM32_BACKUP_DOMAIN_H_
#define STM32_BACKUP_DOMAIN_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_STM32_BACKUP_PROTECTION
/**
 * @brief Request access to protected backup domain
 *
 * This function increments a reference counter each time it is called. Caller
 * is expected to use stm32_backup_domain_disable_access() once it no more
 * needs access to the backup domain protected resource.
 *
 * This function can be called from an interrupt execution context.
 */
void stm32_backup_domain_enable_access(void);

/**
 * @brief Request access and fail if protection remains enabled
 *
 * Unlike stm32_backup_domain_enable_access(), this bounded variant performs a
 * single enable and readback attempt. A successful call owns one reference.
 *
 * @retval 0 Access was enabled.
 * @retval -EACCES Access remained protected.
 */
int stm32_backup_domain_enable_access_checked(void);

/**
 * @brief Release access request to protected backup domain
 *
 * Each call to this function must balance a prior call to
 * stm32_backup_domain_enable_access().
 */
void stm32_backup_domain_disable_access(void);
#else
static inline void stm32_backup_domain_enable_access(void) { }
static inline int stm32_backup_domain_enable_access_checked(void) { return 0; }
static inline void stm32_backup_domain_disable_access(void) { }
#endif /* CONFIG_STM32_BACKUP_PROTECTION */

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* STM32_BACKUP_DOMAIN_H_ */
