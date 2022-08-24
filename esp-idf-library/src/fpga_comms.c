#include "fpga_loader.h"
#include "master_spi.h"
#include "output_trans_pool.h"
#include <driver/spi_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

//#define COMMAND_READ_MEM 0b00000000
#define COMMAND_WRITE_MEM 0b00000001

#define COMMAND_READ_REG 0b00000010
#define COMMAND_WRITE_REG 0b00000011

static const char TAG[] = "fpga_comms";

static spi_device_handle_t fpga_comm_device = NULL;

SemaphoreHandle_t register_read_semaphore = NULL;
static QueueHandle_t register_read_queue = NULL;

//! @brief Handle a finished SPI transaction
//!
//! The vanilla ESP-IDF SPI driver places all finished SPI transactions into a
//! queue, which requires a separate task to clean them up. Our patched
//! version implements a callback (in the interrupt context), which can be
//! used to clean up the transactions resources immediately.
//!
//! In the fpga comms driver, all transactions are performed using memory from
//! the output_trans_pool. For TX-only transactions (memory_write and
//! register_write), no completion conformation is needed, so the callback can
//! directly release the buffer. For RX transactions (register_read), the
//! return value should be stored in a response queue before releasing the
//! buffer. If a read_buffer() command is implemented, this method would need
//! to be extended to handle that case.
static void IRAM_ATTR trans_done_callback(spi_transaction_t* spi_transaction)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (spi_transaction->rxlength > 0) {
        // Notes:
        // * 8 bit delays before actual data is ready
        // * Line endianness needs to be corrected
        const uint32_t value = *((uint32_t*)(spi_transaction->rx_buffer));
        const uint16_t value_shifted = ((value >> 16) & 0xFF) | (value & 0xFF00);

        xQueueSendFromISR(
            register_read_queue,
            &value_shifted,
            &xHigherPriorityTaskWoken);
    }

    output_trans_pool_release(spi_transaction->user);

    if (xHigherPriorityTaskWoken)
        portYIELD_FROM_ISR();
}

esp_err_t IRAM_ATTR fpga_comms_memory_write(uint16_t address, const uint8_t* buffer, int length, int retry_count)
{
    if (fpga_comm_device == NULL) {
        ESP_LOGE(TAG, "fpga_driver not initialized, aborting");
        return ESP_FAIL;
    }

    if (length <= 0) {
        ESP_LOGE(TAG, "Data length 0");
        return ESP_FAIL;
    }

    if (length > CONFIG_FPGA_SPI_BUFFER_SIZE) {
        ESP_LOGE(TAG, "Data length too large, discarding. address:%i buffer:%p length:%i",
            address, buffer, length);
        return ESP_FAIL;
    }

    output_trans_pool_t* output_trans_pool = output_trans_pool_take(retry_count);
    if (output_trans_pool == NULL) {
        ESP_LOGE(TAG, "Could not allocate buffer for SPI transaction");
        return ESP_FAIL;
    }

    uint8_t* dma_buffer = output_trans_pool->buffer;
    spi_transaction_t* spi_transaction = &output_trans_pool->transaction;

    memcpy(dma_buffer, buffer, length);

    const uint16_t word_address = address >> 1;
    const uint16_t word_length = length >> 1;

    memset(spi_transaction, 0, sizeof(*spi_transaction));
    spi_transaction->length = word_length * 16;
    spi_transaction->tx_buffer = dma_buffer;
    spi_transaction->addr = word_address;
    spi_transaction->cmd = COMMAND_WRITE_MEM;
    spi_transaction->user = (void*)output_trans_pool;

    xSemaphoreTake(master_spi_semaphore, portMAX_DELAY);
    esp_err_t ret = spi_device_queue_trans(fpga_comm_device, spi_transaction, 0);
    xSemaphoreGive(master_spi_semaphore);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error queueing transaction, error:%s", esp_err_to_name(ret));
        output_trans_pool_release(output_trans_pool);
    }

    return ret;
}

esp_err_t IRAM_ATTR fpga_comms_memory_read(uint16_t address, uint8_t* buffer, int length, int retry_count) {
    ESP_LOGE(TAG, "memory read not implemented");
    return ESP_FAIL;
}

esp_err_t IRAM_ATTR fpga_comms_register_write(uint16_t address, uint16_t data)
{
    if (fpga_comm_device == NULL) {
        ESP_LOGE(TAG, "fpga_driver not initialized, aborting");
        return ESP_FAIL;
    }

    output_trans_pool_t* output_trans_pool = output_trans_pool_take(5);
    if (output_trans_pool == NULL) {
        ESP_LOGE(TAG, "Could not allocate buffer for SPI transaction");
        return ESP_FAIL;
    }
    spi_transaction_t* spi_transaction = &output_trans_pool->transaction;

    memset(spi_transaction, 0, sizeof(*spi_transaction));
    spi_transaction->flags = SPI_TRANS_USE_TXDATA;
    spi_transaction->length = 16;
    spi_transaction->tx_data[0] = (data >> 8) & 0xFF;
    spi_transaction->tx_data[1] = (data)&0xFF;
    spi_transaction->addr = address;
    spi_transaction->cmd = COMMAND_WRITE_REG;
    spi_transaction->user = (void*)output_trans_pool;

    xSemaphoreTake(master_spi_semaphore, portMAX_DELAY);
    esp_err_t ret = spi_device_queue_trans(fpga_comm_device, spi_transaction, 0);
    xSemaphoreGive(master_spi_semaphore);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error queuing transaction, error:%s", esp_err_to_name(ret));
        output_trans_pool_release(output_trans_pool);
    }

    return ret;
}

esp_err_t IRAM_ATTR fpga_comms_register_read(uint16_t address, uint16_t* data)
{

    if (fpga_comm_device == NULL) {
        ESP_LOGE(TAG, "fpga_driver not initialized, aborting");
        return ESP_FAIL;
    }

    output_trans_pool_t* output_trans_pool = output_trans_pool_take(5);
    if (output_trans_pool == NULL) {
        ESP_LOGE(TAG, "Could not allocate buffer for SPI transaction");
        return ESP_FAIL;
    }

    spi_transaction_t* spi_transaction = &output_trans_pool->transaction;

    // TODO: We don't need to clear this?
    memset(output_trans_pool->buffer, 0, 8);

    memset(spi_transaction, 0, sizeof(*spi_transaction));
    spi_transaction->rx_buffer = output_trans_pool->buffer;
    spi_transaction->length = 0;
    spi_transaction->rxlength = 24; // 8 for turn-around time, 16 for register data
    spi_transaction->addr = address;
    spi_transaction->cmd = COMMAND_READ_REG;
    spi_transaction->user = (void*)output_trans_pool;

    xSemaphoreTake(register_read_semaphore, portMAX_DELAY);

    xSemaphoreTake(master_spi_semaphore, portMAX_DELAY);
    esp_err_t ret = spi_device_queue_trans(fpga_comm_device, spi_transaction, 0);
    xSemaphoreGive(master_spi_semaphore);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error queuing transaction, error:%s", esp_err_to_name(ret));
        output_trans_pool_release(output_trans_pool);

        goto done;
    }

    if (pdPASS != xQueueReceive(register_read_queue, data, pdMS_TO_TICKS(100))) {
        ESP_LOGE(TAG, "Error reading data from address queue");

        ret = ESP_FAIL;
        goto done;
    }

done:
    xSemaphoreGive(register_read_semaphore);
    return ret;
}

static esp_err_t fpga_comms_spi_device_add()
{
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = CONFIG_FPGA_SPI_FREQ_COMMS * 1000000,
        .mode = 3, // CPOL=1 CPHA=1
        .spics_io_num = CONFIG_FPGA_CS_GPIO,
        .queue_size = CONFIG_FPGA_SPI_BUFFER_COUNT,
        .command_bits = 8,
        .address_bits = 16,
        .dummy_bits = 0,
        .duty_cycle_pos = 0,
        .cs_ena_pretrans = 1,
        .cs_ena_posttrans = 0,
        .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_DISCARD_AFTER_POST,
        .pre_cb = NULL,
        .post_cb = trans_done_callback,
    };

    return spi_bus_add_device(FSPI_HOST, &devcfg, &fpga_comm_device);
}

esp_err_t fpga_comms_init()
{
    register_read_queue = xQueueCreate(1, sizeof(uint16_t));

    if (register_read_queue == NULL) {
        ESP_LOGE(TAG, "Error creating read address queue");
        return ESP_FAIL;
    }

    register_read_semaphore = xSemaphoreCreateMutex();

    if (register_read_semaphore == NULL) {
        ESP_LOGE(TAG, "Error creating read address semaphore");
        return ESP_FAIL;
    }

    // Initialize the DMA buffer pool
    output_trans_pool_init();

    // And add an output device to it
    esp_err_t ret = fpga_comms_spi_device_add();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error adding FPGA SPI device");
        return ret;
    }

    return ESP_OK;
}
