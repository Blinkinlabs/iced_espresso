#include "output_trans_pool.h"
#include <driver/spi_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdbool.h>

static const char TAG[] = "output_trans_pool";

output_trans_pool_stats_t output_trans_pool_stats = {
    .requests = 0,
    .retries = 0,
    .failures = 0,
    .double_releases = 0,
    .unowned_releases = 0,
};

static output_trans_pool_t output_trans_pools[CONFIG_FPGA_SPI_BUFFER_COUNT];

void output_trans_pool_init()
{
    for (int index = 0; index < CONFIG_FPGA_SPI_BUFFER_COUNT; index++) {
        output_trans_pool_t* output_trans_pool = &output_trans_pools[index];

        output_trans_pool->in_use = false;
        output_trans_pool->buffer = heap_caps_malloc(CONFIG_FPGA_SPI_BUFFER_SIZE, MALLOC_CAP_DMA);

        if (output_trans_pool->buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for output buffer, index=%i", index);
            // TODO: Fail
            continue;
        }
    }
}

static output_trans_pool_t* IRAM_ATTR get_free_buffer()
{
    // Grab the next available buffer from the pool
    for (int index = 0; index < CONFIG_FPGA_SPI_BUFFER_COUNT; index++) {
        if (!output_trans_pools[index].in_use) {
            output_trans_pools[index].in_use = true;
            return &output_trans_pools[index];
        }
    }

    return NULL;
}

output_trans_pool_t* IRAM_ATTR output_trans_pool_take(int retry_count)
{
    output_trans_pool_stats.requests++;

    output_trans_pool_t* output_trans_pool = NULL;

    while (true) {
        output_trans_pool = get_free_buffer();

        if (output_trans_pool != NULL)
            return output_trans_pool;

        if (retry_count > 0) {
            output_trans_pool_stats.retries++;
            retry_count--;
            vTaskDelay(pdMS_TO_TICKS(POLLING_DELAY_MS));
        } else {
            output_trans_pool_stats.failures++;
            return NULL;
        }
    }
}

void IRAM_ATTR output_trans_pool_release(output_trans_pool_t* output_trans_pool)
{
    for (int index = 0; index < CONFIG_FPGA_SPI_BUFFER_COUNT; index++) {
        if (&output_trans_pools[index] == output_trans_pool) {
            if (!output_trans_pools[index].in_use) {
                output_trans_pool_stats.double_releases++;
                return;
            }

            output_trans_pools[index].in_use = false;
            return;
        }
    }

    output_trans_pool_stats.unowned_releases++;
}
