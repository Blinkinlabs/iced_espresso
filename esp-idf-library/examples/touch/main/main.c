#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <driver/touch_pad.h>
#include "fpga.h"

static const char *TAG = "touch";

// FPGA image ////////////////////////////////////////////////////////////////////////////

const uint8_t top_bin_start asm("_binary_top_bin_start");
const uint8_t top_bin_end asm("_binary_top_bin_end");

const fpga_bin_t fpga_bin = {
    .start = &top_bin_start,
    .end = &top_bin_end,
};

// LED STUFF /////////////////////////////////////////////////////////////////////////////

#define RED_DUTY_REG 0x00F0
#define GREEN_DUTY_REG 0x00F1
#define BLUE_DUTY_REG 0x00F2

static void led_set(
        uint16_t red,
        uint16_t green,
        uint16_t blue)
{
    fpga_comms_write_register(RED_DUTY_REG, red);
    fpga_comms_write_register(GREEN_DUTY_REG, green);
    fpga_comms_write_register(BLUE_DUTY_REG, blue);
}

// MAIN ///////////////////////////////////////////////////////////////////////


void app_main(void)
{
    ESP_ERROR_CHECK(fpga_start(&fpga_bin));

    touch_pad_init();
    touch_pad_config(4);
    touch_pad_config(5);
    touch_pad_config(6);

    touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
    touch_pad_fsm_start();

    while(true) {

        uint32_t r;
        uint32_t g;
        uint32_t b;

        touch_pad_read_raw_data(4, &r);
        touch_pad_read_raw_data(5, &g);
        touch_pad_read_raw_data(6, &b);

//        ESP_LOGI(TAG, ", r:%u g:%u b:%u",r,g,b);
        led_set(r > 15000 ? 65535 : 0,
                g > 15000 ? 65535 : 0,
                b > 15000 ? 65535 : 0
               );

        vTaskDelay(50/portTICK_PERIOD_MS);
    }
}
