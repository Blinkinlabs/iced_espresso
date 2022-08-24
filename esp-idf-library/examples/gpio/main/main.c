#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include "fpga.h"

static const char *TAG = "fpga-gpio";

// FPGA image ////////////////////////////////////////////////////////////////////////////

const uint8_t top_bin_start asm("_binary_top_bin_start");
const uint8_t top_bin_end asm("_binary_top_bin_end");

const fpga_bin_t fpga_bin = {
    .start = &top_bin_start,
    .end = &top_bin_end,
};

// MAIN ///////////////////////////////////////////////////////////////////////

const uint8_t esp_gpios[] = {
    3,
    4,
    5,
    6,
    17,
    43,  // U0TXD
    44,  // U0RXD
};

#define ESP_GPIOS_COUNT (sizeof(esp_gpios)/sizeof(esp_gpios[0]))

void esp_gpio_init() {
    uint64_t pin_bit_mask = 0;
    for(int i = 0; i < ESP_GPIOS_COUNT; i++) {
        pin_bit_mask |= (1ull<<esp_gpios[i]);
    }

    const gpio_config_t config = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = pin_bit_mask,
        .pull_down_en = 0,
        .pull_up_en = 0,
    };

    gpio_config(&config);
}

void esp_gpio_set(uint16_t val)
{
    for(int i = 0; i < sizeof(esp_gpios)/sizeof(esp_gpios[0]); i++) {
        gpio_set_level(esp_gpios[i], (1<<i) == val);
    }
}

esp_err_t gpio_0_set(uint16_t val)
{
    return fpga_comms_register_write(0x0000, val);
}

esp_err_t gpio_1_set(uint16_t val)
{
    return fpga_comms_register_write(0x0001, val);
}

esp_err_t esp_pins_read(uint16_t *val) {
    return fpga_comms_register_read(0x0002, val);
}

#define BUTTON_PIN 0

void button_init() {
    const gpio_config_t config = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1<<BUTTON_PIN),
        .pull_down_en = 0,
        .pull_up_en = 1,
    };
    
    gpio_config(&config);
}

bool button_get() {
    return gpio_get_level(BUTTON_PIN);
}

#define STATUS_LED_PIN 2


//! @brief Initialize the GPIO pin for the Status LED
void status_led_init() {
    const gpio_config_t config = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1<<STATUS_LED_PIN),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    
    gpio_config(&config);
}


//! @brief Enable or disable the blue status LED on the board
//!
//! @param val If true, turn on the LED; otherwise turn off the LED
void status_led_set(const bool val) {
    gpio_set_level(STATUS_LED_PIN, !val);
}

//! @brief Test the GPIO pins between the ESP32 and ICE40
//!
//! This function tests the GPIO connections between the ESP32 and ICE40 part.
//! First, an FPGA bitstream is loaded that sets the GPIOs as input, and
//! allows their values to be read out through a regiser. Next, the ESP32
//! configures its side as outputs, then cycles through each output,
//! setting one at a time and reading all of the values back from the FPGA
//! to verify that they can be read correctly.
//!
//! Note that the FSPI WP and HD signals are currently tested as part of
//! this routine.
void internal_pin_test() {
    const uint8_t pins[] = {
        14,
        9,
        7,
        8,
        18,
        21,
        33,
        34,
        35,
        38,
    };

    uint64_t pin_bit_mask = 0;
    for(int i = 0; i < sizeof(pins)/sizeof(pins[0]); i++) {
        pin_bit_mask |= (1ull<<pins[i]);
    }
    
    const gpio_config_t config = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = pin_bit_mask,
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    
    gpio_config(&config);

    for(int j = 0; j < sizeof(pins)/sizeof(pins[0]); j++) {
        for(int i = 0; i < sizeof(pins)/sizeof(pins[0]); i++) {
            gpio_set_level(pins[i], i == j);
        }

        uint16_t val;
        esp_pins_read(&val);
        ESP_LOGI(TAG, "esp_gpio:%i reg_val:%04x result:%s",
                pins[j],
                val,
                ((1<<j)==val?"PASS":"FAIL")
                );
    }
}

#define FPGA_GPIOS0_COUNT 8
#define FPGA_GPIOS2_COUNT 14

void app_main(void)
{
    status_led_init();
    status_led_set(true);

    ESP_ERROR_CHECK(fpga_start(&fpga_bin));

    esp_gpio_init();

    internal_pin_test();

    uint8_t val = 0;
    while(true) {

        status_led_set(button_get());

        if(val < FPGA_GPIOS0_COUNT) {
            gpio_0_set(1<<val);
            esp_gpio_set(0);
            gpio_1_set(0);
        }
        else if(val < (FPGA_GPIOS0_COUNT + ESP_GPIOS_COUNT)) {
            gpio_0_set(0);
            esp_gpio_set(1<<(val-FPGA_GPIOS0_COUNT));
            gpio_1_set(0);
        }
        else {
            gpio_0_set(0);
            esp_gpio_set(0);
            gpio_1_set(1<<(val - FPGA_GPIOS0_COUNT - ESP_GPIOS_COUNT));
        }

        val = (val + 1) % (FPGA_GPIOS0_COUNT + ESP_GPIOS_COUNT + FPGA_GPIOS2_COUNT);

        vTaskDelay(1);
    }
}
