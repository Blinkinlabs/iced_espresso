#include "fpga_loader.h"
#include "fpga_comms.h"
#include "master_spi.h"
#include "output_trans_pool.h"
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <soc/gpio_sig_map.h>
#include <soc/soc.h>
#include <string.h>
#include <sys/stat.h>

#define CONFIG_FPGA_LOADER_SIZE (CONFIG_FPGA_SPI_BUFFER_SIZE * 4)

typedef struct {
    size_t size;
    void* ctx;
    size_t (*read)(void* buffer, size_t size, void* ctx);
} fpga_firmware_source_t;

static const char TAG[] = "fpga_loader";

//! SPI device used for communication with the ICE40 FPGA
static spi_device_handle_t fpga_update_device = NULL;

//! DMA-capable buffer for writing fpga outputs
static char* dma_buf = NULL;

//! @brief Write a chunk of firmware data to the FPGA
//!
//! \param buffer Buffer to write. Must have MALLOC_CAP_DMA
//! \param length Length of buffer to write. Must be <= CONFIG_FPGA_LOADER_SIZE
//! \return ESP_OK on success
static esp_err_t write_update_block(const char* buffer, int length)
{
    if (length > CONFIG_FPGA_LOADER_SIZE) {
        ESP_LOGE(TAG, "Data length too large, discarding. buffer:%p length:%i",
            buffer, length);
        return ESP_FAIL;
    }

    //ESP_LOGI(TAG, "write_block length:%i buffer:%p", length, buffer);
    spi_transaction_t spi_transaction = {
        .length = length * 8,
        .tx_buffer = buffer,
        .rx_buffer = NULL,
    };

    xSemaphoreTake(master_spi_semaphore, portMAX_DELAY);
    esp_err_t ret = spi_device_transmit(fpga_update_device, &spi_transaction);
    xSemaphoreGive(master_spi_semaphore);
    return ret;
}

//! @brief Convenience function to control the state of the ICE40 reset pin
//!
//! \param value If true, set the pin to logic high, otherwise set the pin low
static void reset_pin_set(bool value)
{
    gpio_set_level(CONFIG_FPGA_CRESET_GPIO, value ? 1 : 0);
}

//! @brief Wait for the CDONE pin to reach the given state
//!
//! \param value If true, wait till the pin is high, otherwise wait for it to be low.
//! \param delay_ms Maximum time to wait for a pin change before timing out
//! \return ESP_OK if pin value observed, ESP_FAIL on a timeout
static esp_err_t cdone_pin_wait_for_value(bool value, uint32_t delay_ms)
{
    TickType_t timeout_time = xTaskGetTickCount() + pdMS_TO_TICKS(delay_ms);

    bool pin_state;
    do {
        pin_state = gpio_get_level(CONFIG_FPGA_CDONE_GPIO) == 1;

        if (xTaskGetTickCount() > timeout_time)
            return ESP_FAIL;
    } while (pin_state != value);

    return ESP_OK;
}

esp_err_t fpga_loader_start() {
    // First, check if there is already an fpga_update_device; in that case,
    // an upload is already in progress, and either _finalize() or _abort()
    // should be called first.
    if(fpga_update_device != NULL) {
        return ESP_FAIL;
    }

    // Register a new SPI device with the ESP driver, to use for programming.
    // This new device has a lower speed and slightly different configuration
    // than the device used for ESP-FPGA application communication.
    const spi_device_interface_config_t devcfg = {
        .clock_speed_hz = CONFIG_FPGA_SPI_FREQ_PROGRAMMING * 1000000,
        .mode = 3,
        .spics_io_num = -1,
        .queue_size = 1,
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .duty_cycle_pos = 0,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .flags = SPI_DEVICE_HALFDUPLEX,
        .pre_cb = NULL,
        .post_cb = NULL,
    };

    esp_err_t ret;
    ret = spi_bus_add_device(FSPI_HOST, &devcfg, &fpga_update_device);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error adding FPGA update SPI device");
        return ret;
    }

    // Claim exclusive use of the SPI bus for the programming device
    ret = spi_device_acquire_bus(fpga_update_device, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error acquiring SPI bus");
        fpga_loader_abort();
        return ret;
    }

    // TODO: The FPGA doesn't seem to program if reset doesnt transition high->low->high, why?
    // This prevented the device from working on a cold boot.
    //    reset_pin_set(1);
    //    vTaskDelay(pdMS_TO_TICKS(10));

    // See 'iCE40-Programming and Configuration Techinical node, Figure 13.3'

    // 1. Drive CRESET_B = 0
    // Pull CRESETB low to put device in reset
    reset_pin_set(0);

    // 2. Drive SPI_SS_B=0, SPI_SCK=1
    // Map the CS pin to the GPIO driver and pull it low
    gpio_set_level(CONFIG_FPGA_CS_GPIO, 0);
    gpio_matrix_out(CONFIG_FPGA_CS_GPIO, SIG_GPIO_OUT_IDX, false, false);

    // 3. Wait minimum of 200ns
    vTaskDelay(1);

    // 4. Release CRESET_B
    reset_pin_set(1);

    // 5. Wait minimum of 1200uS
    vTaskDelay(1);

    // 6. Set SPI_SS_B=1, send 8 dummy clocks
    gpio_set_level(CONFIG_FPGA_CS_GPIO, 1);

    {
        char data[1] = {};
        ret = write_update_block(data, sizeof(data));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error sending dummy bytes");
            fpga_loader_abort();
            return ESP_FAIL;
        }
    }

    gpio_set_level(CONFIG_FPGA_CS_GPIO, 0);

    dma_buf = heap_caps_malloc(CONFIG_FPGA_LOADER_SIZE, MALLOC_CAP_DMA);
    if (dma_buf == NULL) {
        ESP_LOGE(TAG, "Error acquiring dma_buf buffer");
        fpga_loader_abort();
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t fpga_loader_add_chunk(const char* chunk, const int length) {

    // 7. Send configuration image serially on SPI_SI to iCE40, most significant
    //    bit first, on falling edge of SPI_SCK. Send the entire image, without
    //    interruption. Ensure that SPI_SCK frequency is between 1 MHz and 25 MHz.

    // TODO: Check that buffer is malloc'd correctly

    if (length > CONFIG_FPGA_LOADER_SIZE) {
        ESP_LOGE(TAG, "Firmware chunk too large, length:%i max_length:%i",
            length, CONFIG_FPGA_LOADER_SIZE);
        return ESP_FAIL;
    }

    // Copy the chunk data into a DMA-capable memory
    // TODO: Allow the caller to specify that it is already DMA-capable
    memcpy(dma_buf, chunk, length);

    esp_err_t ret = write_update_block(dma_buf, length);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error sending chunk");
        return ret;
    }

    return ESP_OK;
}

esp_err_t fpga_loader_finalize() {
    // 8. Wait for 100 clocks cycles for CDONE to go high

    gpio_set_level(CONFIG_FPGA_CS_GPIO, 1);

    memset(dma_buf, 0, CONFIG_FPGA_LOADER_SIZE);
    write_update_block(dma_buf, 13); //13*8 = 104

    // wait for CDONE signal to go high
    esp_err_t ret = cdone_pin_wait_for_value(true, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error waiting for CDONE to set");
    }

    // 9. Send a minimum of 49 additional dummy bits and 49 additional SPI_SCK
    //    clock cycles (rising-edge to rising-edge) to active the user-I/O pins.

    // Send 49 clocks to finish init
    memset(dma_buf, 0, CONFIG_FPGA_LOADER_SIZE);
    write_update_block(dma_buf, 7); // 7*8 = 56

    // 10. SPI interface pins available as user-defined I/O pins in application.


    // Release resources
    fpga_loader_abort();
    return ret;
}

void fpga_loader_abort() {
    if(dma_buf != NULL) {
        heap_caps_free(dma_buf);
        dma_buf = NULL;
    }

    if(fpga_update_device != NULL) {
        // Release use of the SPI bus
        spi_device_release_bus(fpga_update_device);
 
        // And remove the update device from the SPI bus
        esp_err_t ret = spi_bus_remove_device(fpga_update_device);
        fpga_update_device = NULL;
    }

    // Remap the CS pin back to the hardware CS signal
    // Porting note: This assumes that the output SPI bus uses HSPICS0.
    gpio_set_level(CONFIG_FPGA_CS_GPIO, 1);
    gpio_matrix_out(CONFIG_FPGA_CS_GPIO, FSPICS0_OUT_IDX, false, false); // TODO: FPSI should be using hardware pins, not GPIO
}

static esp_err_t fpga_loader_load(fpga_firmware_source_t* firmware_source)
{
    esp_err_t ret;

    ret = fpga_loader_start();

    size_t bytes_remaining = firmware_source->size;

    while (bytes_remaining > 0) {
        size_t chunk_size = bytes_remaining;
        if (chunk_size > CONFIG_FPGA_LOADER_SIZE)
            chunk_size = CONFIG_FPGA_LOADER_SIZE;

        const size_t read_size = firmware_source->read(dma_buf, chunk_size, firmware_source->ctx);
        if (read_size != chunk_size) {
            //ret = ESP_FAIL;
            ESP_LOGE(TAG, "Error reading firmware, expected:%i read:%i",
                chunk_size, read_size);
            break;
        }

        ret = write_update_block(dma_buf, chunk_size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error sending chunk");
            break;
        }

        bytes_remaining -= chunk_size;
    }

    ret = fpga_loader_finalize();

    return ret;
}

static size_t fpga_loader_file_read(void* buffer, size_t size, void* ctx)
{
    return fread(buffer, 1, size, (FILE*)ctx);
}

esp_err_t fpga_loader_load_from_file(const char* filename)
{
    struct stat file_stat;
    if (stat(filename, &file_stat) == -1) {
        ESP_LOGE(TAG, "Failed to get file statistics");
        return ESP_FAIL;
    }

    const size_t file_size = file_stat.st_size;
    ESP_LOGI(TAG, "File:%s size:%i", filename, file_size);

    ESP_LOGI(TAG, "Opening firmware file");
    FILE* firmware_file;
    firmware_file = fopen(filename, "rb");
    if (firmware_file == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return ESP_FAIL;
    }

    fpga_firmware_source_t firmware_source = {
        .size = file_size,
        .ctx = (void*)firmware_file,
        .read = &fpga_loader_file_read,
    };

    fpga_loader_load(&firmware_source);

    fclose(firmware_file);

    return ESP_OK;
}

//! @brief Load an FPGA image
typedef struct {
    const uint8_t* data; //!< Pointer to starting location of FPGA image
    size_t data_size; //!< Size of the FPGA image
    size_t read_position; //!< Current read position in the image
} rom_read_ctx_t;

//! @brief read a chunk of data from ROM to a buffer
//!
//! @param[out] buffer Memory location to write to
//! @param[in] size Size of buffer
//! @param[in] ctx ROM file context
//! @return Number of bytes copied to the ROM
static size_t fpga_loader_rom_read(void* buffer, size_t size, void* ctx)
{
    rom_read_ctx_t* rom_read_ctx = (rom_read_ctx_t*)ctx;

    // Fail if too much data was requested
    if (size + rom_read_ctx->read_position > rom_read_ctx->data_size)
        return 0;

    memcpy(buffer, rom_read_ctx->data + rom_read_ctx->read_position, size);
    rom_read_ctx->read_position += size;

    return size;
}

esp_err_t fpga_loader_load_from_rom(const fpga_bin_t* fpga_bin)
{
    if(fpga_bin == NULL) {
        return ESP_FAIL;
    }

    if(fpga_bin->end <= fpga_bin->start) {
//        ESP_LOGE(TAG, "Invalid image, start:%08x, end:%08x",
//                fpga_bin->start,
//                fpga_bin->end);
        return ESP_FAIL;
    }

    rom_read_ctx_t read_ctx = {
        .data = fpga_bin->start,
        .data_size = fpga_bin->end - fpga_bin->start,
        .read_position = 0,
    };

    // TODO: size check

    ESP_LOGI(TAG, "Loading FPGA binary, size:%i", read_ctx.data_size);

    fpga_firmware_source_t firmware_source = {
        .size = read_ctx.data_size,
        .ctx = (void*)&read_ctx,
        .read = &fpga_loader_rom_read,
    };

    fpga_loader_load(&firmware_source);

    return ESP_OK;
}

esp_err_t fpga_loader_init()
{
    const gpio_config_t creset_pin = {
        .pin_bit_mask = (1ULL << CONFIG_FPGA_CRESET_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_set_level(CONFIG_FPGA_CRESET_GPIO, 0);
    gpio_config(&creset_pin);

    const gpio_config_t cdone_pin = {
        .pin_bit_mask = (1ULL << CONFIG_FPGA_CDONE_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&cdone_pin);

    return ESP_OK;
}
