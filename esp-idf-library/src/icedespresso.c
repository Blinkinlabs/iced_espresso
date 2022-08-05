#include "icedespresso.h"
#include <driver/gpio.h>

#define BUTTON_PIN 0
#define STATUS_LED_PIN 2

void button_init()
{
    const gpio_config_t config = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1 << BUTTON_PIN),
        .pull_down_en = 0,
        .pull_up_en = 1,
    };

    gpio_config(&config);
}

bool button_pressed()
{
    return !gpio_get_level(BUTTON_PIN);
}

void status_led_init()
{
    const gpio_config_t config = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pin_bit_mask = (1 << STATUS_LED_PIN),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };

    gpio_config(&config);
}

esp_err_t status_led_set(const bool val)
{
    return gpio_set_level(STATUS_LED_PIN, !val);
}

bool status_led_get() {
    return gpio_get_level(STATUS_LED_PIN) == 0;
}
