/**
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/zephyr.h>
#include <zephyr/kernel.h>

BUILD_ASSERT(IS_ENABLED(CONFIG_MULTITHREADING),
	"This file is intended for multi-threading, but single-threading is enabled. "
	"Please check your config build configuration!");

#if defined(NRF5340_XXAA_APPLICATION)
#include <hal/nrf_mutex.h>
#endif /* defined(NRF5340_XXAA_APPLICATION) */

#include "nrf_cc3xx_platform_defines.h"
#include "nrf_cc3xx_platform_mutex.h"
#include "nrf_cc3xx_platform_abort.h"

/** @brief External reference to the platforms abort APIs
 *  	   This is used in case the mutex functions don't
 * 		   provide return values in their APIs.
 */
extern nrf_cc3xx_platform_abort_apis_t platform_abort_apis;

#if CONFIG_CC3XX_MUTEX_LOCK

/** @brief Definition of mutex for symmetric cryptography
 */
K_MUTEX_DEFINE(sym_mutex_int);

/** @brief Definition of mutex for asymmetric cryptography
 */
K_MUTEX_DEFINE(asym_mutex_int);

/** @brief Definition of mutex for power mode changes
*/
K_MUTEX_DEFINE(power_mutex_int);

/** @brief Definition of mutex for heap allocations
 */
K_MUTEX_DEFINE(heap_mutex_int);

#elif CONFIG_CC3XX_ATOMIC_LOCK

/** @brief Definition of mutex for symmetric cryptography
 */
static atomic_t sym_mutex_int;

/** @brief Definition of mutex for asymmetric cryptography
 */
static atomic_t asym_mutex_int;

/** @brief Definition of mutex for power mode changes
*/
static atomic_t power_mutex_int;

/** @brief Definition of mutex for heap allocations
 */
static atomic_t heap_mutex_int;

#elif defined(NRF5340_XXAA_APPLICATION) && NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_HW_MUTEX

typedef enum {
    HW_MUTEX_SYM_CRYPTO = 15,
    HW_MUTEX_ASYM_CRYPTO = 14,
    HW_MUTEX_POWER_MODE = 13,
    HW_MUTEX_HEAP_ALLOC = 12,
} hw_mutex_t;

/** @brief Definition of mutex for symmetric cryptography
 */
static hw_mutex_t sym_mutex_int = HW_MUTEX_SYM_CRYPTO;

/** @brief Definition of mutex for asymmetric cryptography
 */
static hw_mutex_t asym_mutex_int = HW_MUTEX_ASYM_CRYPTO;

/** @brief Definition of mutex for power mode changes
*/
static hw_mutex_t power_mutex_int = HW_MUTEX_POWER_MODE;

/** @brief Definition of mutex for heap allocations
 */
static hw_mutex_t heap_mutex_int = HW_MUTEX_HEAP_ALLOC;

#else
#error "Improper configuration of the lock variant!"
#endif

/** @brief Definition of mutex for random number generation
*/
K_MUTEX_DEFINE(rng_mutex_int);

/** @brief Arbritary number of mutexes the system suppors
 */
#define NUM_MUTEXES 64

/** @brief Structure definition of the mutex slab
 */
struct k_mem_slab mutex_slab;

/** @brief Definition of buffer used for the mutex slabs
 */
char __aligned(4) mutex_slab_buffer[NUM_MUTEXES * sizeof(struct k_mutex)];

/**@brief Definition of RTOS-independent symmetric cryptography mutex
 * with NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_VALID set to indicate that
 * allocation is unneccesary
*/
static nrf_cc3xx_platform_mutex_t sym_mutex = {
    .mutex = &sym_mutex_int,
    .flags = IS_ENABLED(CONFIG_CC3XX_ATOMIC_LOCK) ?
                NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_ATOMIC :
                IS_ENABLED(CONFIG_CC3XX_HW_MUTEX_LOCK) ?
                    NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_HW_MUTEX :
                    NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_VALID
};

/**@brief Definition of RTOS-independent asymmetric cryptography mutex
 * with NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_VALID set to indicate that
 * allocation is unneccesary
*/
static nrf_cc3xx_platform_mutex_t asym_mutex = {
    .mutex = &asym_mutex_int,
    .flags = IS_ENABLED(CONFIG_CC3XX_ATOMIC_LOCK) ?
                NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_ATOMIC :
                IS_ENABLED(CONFIG_CC3XX_HW_MUTEX_LOCK) ?
                    NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_HW_MUTEX :
                    NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_VALID
};

/**@brief Definition of RTOS-independent random number generation mutex
 * with NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_VALID set to indicate that
 * allocation is unneccesary
*/
static nrf_cc3xx_platform_mutex_t rng_mutex = {
	.mutex = &rng_mutex_int,
	.flags = NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_VALID
};

/**@brief Definition of RTOS-independent power management mutex
 * with NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_VALID set to indicate that
 * allocation is unneccesary
*/
static nrf_cc3xx_platform_mutex_t power_mutex = {
    .mutex = &power_mutex_int,
    .flags = IS_ENABLED(CONFIG_CC3XX_ATOMIC_LOCK) ?
                NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_ATOMIC :
                IS_ENABLED(CONFIG_CC3XX_HW_MUTEX_LOCK) ?
                    NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_HW_MUTEX :
                    NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_VALID
};

/** @brief Definition of RTOS-independent heap allocation mutex
 *  with NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_VALID set to indicate that
 *  allocation is unneccesary
 *
 * @note This symbol can't be static as it is referenced in the replacement
 *       file mbemory_buffer_alloc.c inside the heap structure.
 */
nrf_cc3xx_platform_mutex_t heap_mutex = {
    .mutex = &heap_mutex_int,
    .flags = IS_ENABLED(CONFIG_CC3XX_ATOMIC_LOCK) ?
                NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_ATOMIC :
                IS_ENABLED(CONFIG_CC3XX_HW_MUTEX_LOCK) ?
                    NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_HW_MUTEX :
                    NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_VALID
};

/**@brief static function to initialize a mutex
 */
static void mutex_init_platform(nrf_cc3xx_platform_mutex_t *mutex) {
    int ret;
    struct k_mutex * p_mutex;

    /* Ensure that the mutex is valid (not NULL) */
    if (mutex == NULL) {
        platform_abort_apis.abort_fn(
            "mutex_init called with NULL parameter");
    }
    /* Atomic mutex has been initialized statically */
    if (mutex->flags == NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_ATOMIC ||
        mutex->flags == NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_HW_MUTEX) {
        return;
    }

    /* Allocate if this has not been initialized statically */
    if (mutex->flags == NRF_CC3XX_PLATFORM_MUTEX_MASK_INVALID &&
        mutex->mutex == NULL) {
        /* Allocate some memory for the mutex */
        ret = k_mem_slab_alloc(&mutex_slab, &mutex->mutex, K_FOREVER);
        if(ret != 0 || mutex->mutex == NULL)
        {
            /* Allocation failed. Abort all operations */
            platform_abort_apis.abort_fn(
                "Could not allocate mutex before initializing");
        }

        memset(mutex->mutex, 0, sizeof(struct k_mutex));

        /** Set a flag to ensure that mutex is deallocated by the freeing
         * operation
         */
        mutex->flags |= NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_ALLOCATED;
    }

    p_mutex = (struct k_mutex *)mutex->mutex;
    k_mutex_init(p_mutex);

    /* Set the mask to indicate that the mutex is valid */
    mutex->flags |= NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_VALID;
}


/** @brief Static function to free a mutex
 */
static void mutex_free_platform(nrf_cc3xx_platform_mutex_t *mutex) {
    /* Ensure that the mutex is valid (not NULL) */
    if (mutex == NULL) {
        platform_abort_apis.abort_fn(
            "mutex_init called with NULL parameter");
    }

    /* Check if we are freeing a mutex that is atomic */
    if (mutex->flags == NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_ATOMIC ||
        mutex->flags == NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_HW_MUTEX) {
        /*Nothing to free*/
        return;
    }

    /* Check if we are freeing a mutex that isn't initialized */
    if (mutex->flags == NRF_CC3XX_PLATFORM_MUTEX_MASK_INVALID) {
        /*Nothing to free*/
        return;
    }

    /* Check if the mutex was allocated or being statically defined */
    if ((mutex->flags & NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_ALLOCATED) != 0) {
        k_mem_slab_free(&mutex_slab, &mutex->mutex);
        mutex->mutex = NULL;
    }
    else {
        memset(mutex->mutex, 0, sizeof(struct k_mutex));
    }

    /* Reset the mutex to invalid state */
    mutex->flags = NRF_CC3XX_PLATFORM_MUTEX_MASK_INVALID;
}


/** @brief Static function to lock a mutex
 */
static int32_t mutex_lock_platform(nrf_cc3xx_platform_mutex_t *mutex) {
    int ret;
    struct k_mutex * p_mutex;

    /* Ensure that the mutex param is valid (not NULL) */
    if(mutex == NULL) {
        return NRF_CC3XX_PLATFORM_ERROR_PARAM_NULL;
    }

    switch (mutex->flags) {
    case NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_ATOMIC:
        return atomic_cas((atomic_t *)mutex->mutex, 0, 1) ?
                       NRF_CC3XX_PLATFORM_SUCCESS :
                       NRF_CC3XX_PLATFORM_ERROR_MUTEX_FAILED;

#if defined(NRF5340_XXAA_APPLICATION)

    case NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_HW_MUTEX:
        return nrf_mutex_lock(NRF_MUTEX, *((uint8_t *)mutex->mutex)) ?
                       NRF_CC3XX_PLATFORM_SUCCESS :
                       NRF_CC3XX_PLATFORM_ERROR_MUTEX_FAILED;

#endif /* defined(NRF5340_XXAA_APPLICATION) */

    default:
        /* Ensure that the mutex has been initialized */
        if (mutex->flags == NRF_CC3XX_PLATFORM_MUTEX_MASK_INVALID) {
            return NRF_CC3XX_PLATFORM_ERROR_MUTEX_NOT_INITIALIZED;
        }

        p_mutex = (struct k_mutex *)mutex->mutex;

        ret = k_mutex_lock(p_mutex, K_FOREVER);
        if (ret == 0) {
            return NRF_CC3XX_PLATFORM_SUCCESS;
        } else {
            return NRF_CC3XX_PLATFORM_ERROR_MUTEX_FAILED;
        }
    }
}

/** @brief Static function to unlock a mutex
 */
static int32_t mutex_unlock_platform(nrf_cc3xx_platform_mutex_t *mutex) {
    struct k_mutex * p_mutex;

    /* Ensure that the mutex param is valid (not NULL) */
    if(mutex == NULL) {
        return NRF_CC3XX_PLATFORM_ERROR_PARAM_NULL;
    }

    switch (mutex->flags)
    {
    case NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_ATOMIC:
        return atomic_cas((atomic_t *)mutex->mutex, 1, 0) ?
                       NRF_CC3XX_PLATFORM_SUCCESS :
                       NRF_CC3XX_PLATFORM_ERROR_MUTEX_FAILED;


#if defined(NRF5340_XXAA_APPLICATION)

    case NRF_CC3XX_PLATFORM_MUTEX_MASK_IS_HW_MUTEX:
        nrf_mutex_unlock(NRF_MUTEX, *((uint8_t *)mutex->mutex));
        return NRF_CC3XX_PLATFORM_SUCCESS;

#endif /* defined(NRF5340_XXAA_APPLICATION) */

    default:
        /* Ensure that the mutex has been initialized */
        if (mutex->flags == NRF_CC3XX_PLATFORM_MUTEX_MASK_INVALID) {
            return NRF_CC3XX_PLATFORM_ERROR_MUTEX_NOT_INITIALIZED;
        }

        p_mutex = (struct k_mutex *)mutex->mutex;

        k_mutex_unlock(p_mutex);
        return NRF_CC3XX_PLATFORM_SUCCESS;
    }
}

/**@brief Constant definition of mutex APIs to set in nrf_cc3xx_platform
 */
static const nrf_cc3xx_platform_mutex_apis_t mutex_apis =
{
    .mutex_init_fn = mutex_init_platform,
    .mutex_free_fn = mutex_free_platform,
    .mutex_lock_fn = mutex_lock_platform,
    .mutex_unlock_fn = mutex_unlock_platform
};


/** @brief Constant definition of mutexes to set in nrf_cc3xx_platform
 */
static const nrf_cc3xx_platform_mutexes_t mutexes = {
            .sym_mutex = &sym_mutex,
            .asym_mutex = &asym_mutex,
            .rng_mutex = &rng_mutex,
            .reserved = NULL,
            .power_mutex = &power_mutex,
};

/** @brief Function to initialize the nrf_cc3xx_platform mutex APIs
 */
void nrf_cc3xx_platform_mutex_init(void)
{
    k_mem_slab_init(&mutex_slab,
                mutex_slab_buffer,
                sizeof(struct k_mutex),
                NUM_MUTEXES);

    nrf_cc3xx_platform_set_mutexes(&mutex_apis, &mutexes);
}
