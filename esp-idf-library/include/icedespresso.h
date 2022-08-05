#pragma once

#include <esp_err.h>
#include <stdbool.h>

void button_init();

//! @brief Returns true if the on-board button is pressed
bool button_pressed();

//! @brief Initialize the GPIO pin for the Status LED
void status_led_init();

//! @brief Enable or disable the blue status LED on the board
//!
//! @param val If true, turn on the LED; otherwise turn off the LED
esp_err_t status_led_set(const bool val);

bool status_led_get();
