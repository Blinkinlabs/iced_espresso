#pragma once

#include "fpga_comms.h"
#include "fpga_loader.h"
#include "master_spi.h"

esp_err_t fpga_start(const fpga_bin_t* fpga_bin);
