#pragma once

#include <driver/spi_master.h>
#include <stdbool.h>
#include <stdint.h>

//! @defgroup output_trans_pool Output SPI buffer pool
//!
//! @brief Buffer pool for SPI bulk transactions
//!
//! All buffers in the pool are created with MALLOC_CAP_DMA, so that
//! they can be used directly with the SPI DMA hardware.
//!
//! @{

#define POLLING_DELAY_MS 10

//! Output transaction pool entry
//!
//! Note: Buffers must be allocated at runtime, so they can be aligned for DMA transactions
typedef struct {
    bool in_use; //!< True if the buffer is in use
    spi_transaction_t transaction; //!< Type of transaction stored in this buffer
    uint8_t* buffer; //!< Buffer allocated to this entry. The buffer is owned by the
        //!< transaction pool, and should not be freed by the user.
} output_trans_pool_t;

//! Output transaction pool statistics
typedef struct {
    uint32_t requests; //!< Number of times a buffer has been requested
    uint32_t retries; //!< Number of times that a buffer wasn't available, and a retry was needed
    uint32_t failures; //!< Number of times that a buffer failed to be allocated
    uint32_t double_releases; //!< Number of times that a buffer was released when it wasn't in use
    uint32_t unowned_releases; //!< Number of requests to free a buffer not owned by the pool
} output_trans_pool_stats_t;

extern output_trans_pool_stats_t output_trans_pool_stats;

//! @brief Initialize the output transaction pool
void output_trans_pool_init();

//! @brief Print output buffer pool statistics
void output_trans_pool_stats_print();

//! @brief Take a buffer from the pool
//!
//! This uses vTaskDelay() and not be called from an interrupt context
//!
//! @param[in] retry_count Number of times to try (with polling delay) before giving up
//! @return Pointer to a buffer pool object if successful, NULL if unsuccessful. Pointers
//!         are owned by the buffer pool, and must not be freed. To release a buffer,
//!         pass it in a call to @ref output_trans_pool_release().
output_trans_pool_t* output_trans_pool_take(int retry_count);

//! @brief Release a buffer to the pool
//!
//! This may be called from an interrupt context
//!
//! @param output_trans_pool Pointer to a buffer pool object to release.
void output_trans_pool_release(output_trans_pool_t* output_trans_pool);

//! @}
