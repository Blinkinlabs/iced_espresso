#pragma once

#include <esp_err.h>

//! @defgroup fpga_comms FPGA communication module
//!
//! @brief Routines for sending data to the FPGA with the SuperSweet image loaded
//!
//! @{

//! @brief Initialize the FPGA communication channel
//!
//! @return ESP_OK on success, error code otherwise
esp_err_t fpga_comms_init();

//! @brief Write a buffer of data to the FPGA memory
//!
//! The passed buffer will be automatically copied into a DMA-capable buffer,
//! then sent over the SPI bus to the FPGA.
//!
//! @param[in] address Byte address to write to (must be 16-bit aligned)
//! @param[in] buffer Source buffer to read from.
//! @param[in] length Number of bytes to write (must be a multiple of 16 bits)
//! @param[in] retry_count Number of times to attempt transmission before failing.
//! @return ESP_OK on success, error otherwise.
esp_err_t fpga_comms_send_buffer(uint16_t address, const uint8_t* buffer, int length, int retry_count);

//! @brief Write a 16-bit register in the FPGA memory
//!
//! @param[in] address Byte address to write to (must be 16-bit aligned)
//! @param[in] data Data to write to the register
//! @return ESP_OK on success, error otherwise.
esp_err_t fpga_comms_write_register(uint16_t address, uint16_t data);

esp_err_t fpga_comms_read_register(uint16_t address, uint16_t* data);

//! @}
