#include "fpga.h"

esp_err_t fpga_start(const fpga_bin_t* fpga_bin)
{
    master_spi_init();
    fpga_comms_init();
    fpga_loader_init();
    fpga_loader_load_from_rom(fpga_bin);

    return ESP_OK;
}
