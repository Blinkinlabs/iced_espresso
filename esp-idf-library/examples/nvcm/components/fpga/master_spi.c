#include "master_spi.h"
#include <driver/spi_master.h>
#include <esp_log.h>

static const char * TAG = "MASTER_SPI";

SemaphoreHandle_t master_spi_semaphore;

esp_err_t master_spi_init()
{
    if (master_spi_semaphore == NULL)
        master_spi_semaphore = xSemaphoreCreateMutex();

    spi_bus_config_t buscfg = {
        .mosi_io_num = CONFIG_FPGA_MOSI_GPIO,
        .miso_io_num = CONFIG_FPGA_MISO_GPIO,
        .sclk_io_num = CONFIG_FPGA_SCLK_GPIO,
        .quadwp_io_num = CONFIG_FPGA_WP_GPIO,
        .quadhd_io_num = CONFIG_FPGA_HD_GPIO,
        .max_transfer_sz = CONFIG_FPGA_SPI_BUFFER_SIZE * 4,
        //.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_IOMUX_PINS, // TODO: Fix WP connection in RevB
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
    };

    return spi_bus_initialize(FSPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
}
