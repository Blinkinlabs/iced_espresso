#pragma once

#include <esp_err.h>


//! @brief Use the ESP32 APLL to generate a reference clock for the ICE40 FPGA
//!
//! @param f_target Target frequency to generate, in MHz
//! @return ESP_OK if successful
esp_err_t fpga_clock_start(const float f_target);

//! @brief Stop the ICE40 reference clock
esp_err_t fpga_clock_stop();
