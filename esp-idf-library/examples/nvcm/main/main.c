#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include "fpga_comms.h"
#include "fpga_loader.h"
#include "master_spi.h"

static const char *TAG = "nvcm_test";


// MAIN ///////////////////////////////////////////////////////////////////////


esp_err_t esp_pins_read(uint16_t *val) {
    return fpga_comms_read_register(0x0002, val);
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

void print_id(const uint8_t id) {
    struct {
        uint8_t id;
        const char *name;
    } nvcm_id_table[] = {
        { .id = 0x06, .name = "ICE40LP8K / ICE40HX8K" },
        { .id = 0x07, .name = "ICE40LP4K / ICE40HX4K" },
        { .id = 0x08, .name = "ICE40LP1K / ICE40HX1K" },
        { .id = 0x09, .name = "ICE40LP384" },
        { .id = 0x0E, .name = "ICE40LP1K_SWG16" },
        { .id = 0x0F, .name = "ICE40LP640_SWG16" },
        { .id = 0x10, .name = "ICE5LP1K" },
        { .id = 0x11, .name = "ICE5LP2K" },
        { .id = 0x12, .name = "ICE5LP4K" },
        { .id = 0x14, .name = "ICE40UL1K" },
        { .id = 0x15, .name = "ICE40UL640" },
        { .id = 0x20, .name = "ICE40UP5K" },
        { .id = 0x21, .name = "ICE40UP3K" },
    };
    
    const char *name = NULL;
    for (int i = 0; i < (sizeof(nvcm_id_table) / sizeof(*nvcm_id_table)); i++) {
        if (nvcm_id_table[i].id == id) {
            name = nvcm_id_table[i].name;
            break;
        }
    }
    
    if(name)
        ESP_LOGI(TAG, "id:%i name:%s", id, name);
    else
        ESP_LOGI(TAG, "id:%i name:%s", id, "?");
}

void app_main(void)
{
    status_led_init();
    status_led_set(true);

    ESP_ERROR_CHECK(master_spi_init());
    ESP_ERROR_CHECK(fpga_comms_init());
    ESP_ERROR_CHECK(fpga_loader_init());

    uint8_t id;
    fpga_loader_read_part_id(&id);
    ESP_LOGI(TAG, "fpga, id:%i", id);

    print_id(id);

    fpga_loader_load_from_rom("");

}
