#pragma once

#include <esp_err.h>

//! @defgroup fpga_loader FPGA loader module
//!
//! @brief Interface for loading an FPGA configuration blob into the FPGA
//!
//! Starting with RevC, the SuperSweet hardware does not include a seperate SPI
//! flash for the FPGA, and instead it needs to be loaded at boot from the ESP32.
//! These routines implement the ICE40 soft load procedure.
//!
//! @{

//! @brief Load the FPGA from a file in the VFS
//!
//! This routine will reset the FPGA, put it in external boot mode, then initialize
//! it using the contents of the specified file. The supported file type is a
//! binary blob of the FPGA image.
//!
//! @param[in] filename Path of the file (in the VFS) containing the FPGA binary
//! @return ESP_OK on success, error code otherwise
esp_err_t fpga_loader_load_from_file(const char* filename);

//! @brief Load the FPGA from a built-in ROM file
//!
//! This routine will reset the FPGA, put it in external boot mode, then initialize
//! it using the contents of the specified file. The supported file type is a
//! binary blob of the FPGA image.
//!
//! @param[in] filename Name of the file (in the fpga_bin structure)
//! @return ESP_OK on success, error code otherwise
esp_err_t fpga_loader_load_from_rom(const char* filename);

esp_err_t fpga_loader_read_part_id(uint8_t *id);

//! @brief Initialize the hardware needed for loading
//!
//! @return ESP_OK on success, error code otherwise
esp_err_t fpga_loader_init();

//! @}
