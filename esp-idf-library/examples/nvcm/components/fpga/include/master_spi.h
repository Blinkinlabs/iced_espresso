#pragma once

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

//! @defgroup master_spi Master SPI bus
//!
//! @brief Utilities to share the HSPI bus between multiple threads on the ESP32.
//!
//! Use init() to initialize the bus and access semaphore, then wrap all SPI
//! transactions wia call to the semaphore:
//!
//!     SemaphoreTake(master_spi_semaphore, portMAX_DELAY);
//!     spi_device_transmit(***);
//!     xSemaphoreGive(master_spi_semaphore);
//!
//! @{

//! Master SPI semaphore. Acquire this before using the master SPI bus
extern SemaphoreHandle_t master_spi_semaphore;

//! @brief Initialize the HSPI SPI bus in master mode
//!
//! @return ESP_OK on success, error code otherwise
esp_err_t master_spi_init();

//! @}
