#include "fpga_loader.h"
#include "fpga_bin.h"
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

static spi_device_handle_t fpga_update_device = NULL;

static esp_err_t update_spi_device_add()
{
    spi_device_interface_config_t devcfg = {
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

    return spi_bus_add_device(FSPI_HOST, &devcfg, &fpga_update_device);
}

static esp_err_t update_spi_device_remove()
{
    esp_err_t ret = spi_bus_remove_device(fpga_update_device);
    fpga_update_device = NULL;

    return ret;
}

static esp_err_t write_update_block(const uint8_t* buffer, int length)
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

static esp_err_t write_read(
        const uint8_t* tx_buffer,
        const int tx_length,
        uint8_t* rx_buffer,
        const int rx_length)
{
    if (tx_length > CONFIG_FPGA_LOADER_SIZE) {
        ESP_LOGE(TAG, "Data length too large, discarding. tx_buffer:%p tx_length:%i",
            tx_buffer,
            tx_length);
        return ESP_FAIL;
    }

    //ESP_LOGI(TAG, "write_block length:%i buffer:%p", length, buffer);
    spi_transaction_t spi_transaction = {
        .length = tx_length * 8,
        .tx_buffer = tx_buffer,
        .rxlength = rx_length * 8,
        .rx_buffer = rx_buffer
    };

    xSemaphoreTake(master_spi_semaphore, portMAX_DELAY);
    esp_err_t ret = spi_device_transmit(fpga_update_device, &spi_transaction);
    xSemaphoreGive(master_spi_semaphore);
    return ret;
}

static void reset_pin_set(bool value)
{
    gpio_set_level(CONFIG_FPGA_CRESET_GPIO, value ? 1 : 0);
}

static void cs_pin_set(bool value)
{
    gpio_set_level(CONFIG_FPGA_CS_GPIO, value ? 1 : 0);
}

static esp_err_t cdone_pin_wait_for_value(bool value, uint32_t delay_ms)
{
    TickType_t timeout_time = xTaskGetTickCount() + pdMS_TO_TICKS(delay_ms);

    // TODO: Timeout
    bool pin_state;
    do {
        pin_state = gpio_get_level(CONFIG_FPGA_CDONE_GPIO) == 1;

        if (xTaskGetTickCount() > timeout_time)
            return ESP_FAIL;
    } while (pin_state != value);

    return ESP_OK;
}

#define FC_RD   0x03
#define FC_RSR1 0x05

static uint8_t nvcm_read_status()
{
    uint8_t dummy[126];
    memset(dummy, 0, sizeof(dummy));


    //mpsse_send_dummy_bytes(125);
    write_update_block(dummy, 125);

    //sram_chip_select();
    cs_pin_set(0);

    uint8_t command[] = { FC_RSR1 };
    uint8_t status;

    //mpsse_xfer_spi(data, sizeof(data));
    write_read(command, 1, &status, 1);

    //sram_chip_deselect();
    cs_pin_set(1);

    //mpsse_send_dummy_bytes(126);
    write_update_block(dummy, 126);

    ESP_LOGI(TAG, "Status: %02x", status);
    return status;
}

static bool nvcm_select_bank(int bank)
{
    uint8_t bank_select_command[] = { 0x83, 0x00, 0x00, 0x25, 0x00 };
    
    if ( (bank < 0) || (bank > 2) ) return false;
    
    bank_select_command[4] |= bank << 4;
    
    //sram_chip_select();
    cs_pin_set(0);

    //mpsse_send_spi(bank_select_command, sizeof(bank_select_command));
    write_update_block(bank_select_command, sizeof(bank_select_command));

    //sram_chip_deselect();
    cs_pin_set(1);
    
    return (0 == (nvcm_read_status() & 0xC1));
}

static void nvcm_read_bank(int bank, int addr, uint8_t *data, int n)
{
    if (!nvcm_select_bank(bank)) return;
    
    uint8_t read_command[4] = {
        FC_RD,
        (uint8_t)(addr >> 16),
        (uint8_t)(addr >> 8),
        (uint8_t)addr
    };
    
    //sram_chip_select();
    cs_pin_set(0);

    //mpsse_send_spi(read_command, sizeof(read_command));
    write_update_block(read_command, sizeof(read_command));

    //mpsse_send_dummy_bytes(9 /* read latency */);
    uint8_t dummy[9];
    memset(dummy, 0, sizeof(dummy));
    write_update_block(dummy, sizeof(dummy));

    memset(data, 0, n);

    //mpsse_xfer_spi(data, n);
    write_read(NULL, 0, data, n);

    //sram_chip_deselect();
    cs_pin_set(1);
}

static esp_err_t nvcm_mode_entry()
{
    esp_err_t ret;

    uint8_t nvcm_entry_sequence[] = { 0x7e, 0xaa, 0x99, 0x7e, 0x01, 0x0e };

    reset_pin_set(0);
    vTaskDelay(1);

    // sram_reset()
    cs_pin_set(0);
    //reset_pin_set(0);

    vTaskDelay(1);

    // sram_chip_select()
    //cs_pin_set(0);
    reset_pin_set(1);

    vTaskDelay(1);

    cs_pin_set(1);

//    ret = cdone_pin_wait_for_value(true, 100);
//    if (ret != ESP_OK) {
//        ESP_LOGE(TAG, "Error waiting for CDONE to set");
//        return ESP_FAIL;
//    }

    // send knock code
    ret = write_update_block(nvcm_entry_sequence, sizeof(nvcm_entry_sequence));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error sending knock code");
        return ESP_FAIL;
    }
    
    // sram_chip_deselect()
    cs_pin_set(1);
    //reset_pin_set(1);

    int retries = 32;
    while (retries--) {
        if (0 == (nvcm_read_status() & 0xC1))
            return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to enter NVCM mode");
    return ESP_FAIL;
}

esp_err_t fpga_loader_read_part_id(uint8_t *id)
{
    esp_err_t ret;

    // Add the update device to the SPI bus
    ret = update_spi_device_add();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error adding FPGA update SPI device");
        goto spi_device_add_error;
    }

    // Claim exclusive use of the SPI bus for the update device
    ret = spi_device_acquire_bus(fpga_update_device, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error acquiring SPI bus");
        goto spi_bus_acquire_error;
    }

    gpio_set_level(CONFIG_FPGA_CS_GPIO, 0);
    gpio_matrix_out(CONFIG_FPGA_CS_GPIO, SIG_GPIO_OUT_IDX, false, false);

    ret = nvcm_mode_entry();
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "Error entering NVCM mode");
        goto reset_cs_pin_mux;
    }

    nvcm_read_bank(2, 0, id, 1); /* Device ID is stored in a single byte at address 0 in NVCM bank 2 */
    ret = ESP_OK;

reset_cs_pin_mux:
    gpio_set_level(CONFIG_FPGA_CS_GPIO, 1);
    gpio_matrix_out(CONFIG_FPGA_CS_GPIO, FSPICS0_OUT_IDX, false, false); // TODO: FPSI should be using hardware pins, not GPIO

//cdone_clear_error:
    // Release use of the SPI bus
    spi_device_release_bus(fpga_update_device);

spi_bus_acquire_error:
    // And remove the update device from the SPI bus
    update_spi_device_remove();

spi_device_add_error:
    return ret;
}

static esp_err_t fpga_loader_load(fpga_firmware_source_t* firmware_source)
{
    esp_err_t ret;

    // Add the update device to the SPI bus
    ret = update_spi_device_add();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error adding FPGA update SPI device");
        goto spi_device_add_error;
    }

    // Claim exclusive use of the SPI bus for the update device
    ret = spi_device_acquire_bus(fpga_update_device, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error acquiring SPI bus");
        goto spi_bus_acquire_error;
    }

    // TODO: The FPGA doesn't seem to program if reset doesnt transition high->low->high, why?
    // This prevented the device from working on a cold boot.
    //    reset_pin_set(1);
    //    vTaskDelay(pdMS_TO_TICKS(10));

    // See 'iCE40-Programming and Configuration Techinical node, Figure 13.3'

    // 1. Drive CREST_B = 0
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
        uint8_t data[1] = {};
        ret = write_update_block(data, sizeof(data));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error sending dummy bytes");
            goto cdone_clear_error;
        }
    }

    gpio_set_level(CONFIG_FPGA_CS_GPIO, 0);

    // 7. Send configuration image serially on SPI_SI to iCE40, most significant
    //    bit first, on falling edge of SPI_SCK. Send the entire image, without
    //    interruption. Ensure that SPI_SCK frequency is between 1 MHz and 25 MHz.

    // Write flash pages
    uint8_t* data = heap_caps_malloc(CONFIG_FPGA_LOADER_SIZE, MALLOC_CAP_DMA);
    if (data == NULL) {
        ret = ESP_FAIL;
        ESP_LOGE(TAG, "Error acquiring data buffer");
        goto cdone_clear_error;
    }

    size_t bytes_remaining = firmware_source->size;

    while (bytes_remaining > 0) {
        size_t chunk_size = bytes_remaining;
        if (chunk_size > CONFIG_FPGA_LOADER_SIZE)
            chunk_size = CONFIG_FPGA_LOADER_SIZE;

        const size_t read_size = firmware_source->read(data, chunk_size, firmware_source->ctx);
        if (read_size != chunk_size) {
            //ret = ESP_FAIL;
            ESP_LOGE(TAG, "Error reading firmware, expected:%i read:%i",
                chunk_size, read_size);
            break;
        }

        ret = write_update_block(data, chunk_size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error sending chunk");
            break;
        }

        bytes_remaining -= chunk_size;
    }

    // TODO: Drop here if ret != ESP_OK

    // 8. Wait for 100 clocks cycles for CDONE to go high

    gpio_set_level(CONFIG_FPGA_CS_GPIO, 1);

    memset(data, 0, CONFIG_FPGA_LOADER_SIZE);
    write_update_block(data, 13); //13*8 = 104

    // wait for CDONE signal to go high
    ret = cdone_pin_wait_for_value(true, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error waiting for CDONE to set");
    }

    // 9. Send a minimum of 49 additional dummy bits and 49 additional SPI_SCK
    //    clock cycles (rising-edge to rising-edge) to active the user-I/O pins.

    // Send 49 clocks to finish init
    memset(data, 0, CONFIG_FPGA_LOADER_SIZE);
    write_update_block(data, 7); // 7*8 = 56

    // 10. SPI interface pins available as user-defined I/O pins in application.

    // Remap the CS pin back to the hardware CS signal
    // TODO: Note that if the output SPI device was not registered first,
    // it might not have gotten HSPICS0, but we don't have an easy way to
    // detect that here.
    gpio_set_level(CONFIG_FPGA_CS_GPIO, 1);
    gpio_matrix_out(CONFIG_FPGA_CS_GPIO, FSPICS0_OUT_IDX, false, false); // TODO: FPSI should be using hardware pins, not GPIO

    heap_caps_free(data);

cdone_clear_error:
    // Release use of the SPI bus
    spi_device_release_bus(fpga_update_device);

spi_bus_acquire_error:
    // And remove the update device from the SPI bus
    update_spi_device_remove();

spi_device_add_error:
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

esp_err_t fpga_loader_load_from_rom(const char* filename)
{
    // TODO: Look up file in ROM table

    rom_read_ctx_t read_ctx = {
        .data = fpga_bin_entries[0].start,
        .data_size = fpga_bin_entries[0].end - fpga_bin_entries[0].start,
        .read_position = 0,
    };

    ESP_LOGI(TAG, "File:%s size:%i", fpga_bin_entries[0].name, read_ctx.data_size);

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
