#pragma once

#include <esp_err.h>
#include <stdbool.h>

//! @brief Start an OTA update
esp_err_t ota_start(const size_t image_length);

//! @brief Add data to an in-progress OTA update
esp_err_t ota_add_chunk(const char* chunk, const int length);

//! @brief Finish an OTA operation
esp_err_t ota_finalize();

esp_err_t ota_abort();

//! @brief Initialize the OTA system
//!
//! Check if the partition is marked as pending verify, and if so, mark it as valid. This
//! function should be called immediately at each boot.
esp_err_t ota_init();
