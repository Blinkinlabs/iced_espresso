#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include "fpga.h"
#include "driver/i2s.h"

static const char *TAG = "fpga-gpio";

// FPGA image ////////////////////////////////////////////////////////////////////////////

const uint8_t top_bin_start asm("_binary_top_bin_start");
const uint8_t top_bin_end asm("_binary_top_bin_end");

const fpga_bin_t fpga_bin = {
    .start = &top_bin_start,
    .end = &top_bin_end,
};

// IO ///////////////////////////////////////////////////////////////////////


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

// Clock //////////////////////////////////////////////////////////////////////

// Copied from ESP-IDF V4.3: components/driver/i2s.c
#include "soc/rtc.h"
#include "hal/gpio_hal.h"
#include "driver/i2s.h"

bool calc_apll_setting(const float f_target, int *sdm0, int *sdm1, int *sdm2, int *odiv)
{
    const int f_xtal = rtc_clk_xtal_freq_get();

    bool solved = false;    // True if a valid solution could be found
    float error_abs = 0;    // Absolute error in frequency setting
    float f_achieved = 0;

    for (int odiv_cur = 0; odiv_cur < 32; odiv_cur++) {
        const float denominator_cur = 2*(odiv_cur + 2);

        // ESP32-S2 TRM section 6.2.7: The operating frequency range of the
        // numerator is 350 MHz ~ 500 MHz.
        const float numerator_target = f_target * denominator_cur;
        if ((numerator_target > 500) || (numerator_target < 350)) {
            continue;
        }

        // SDM has 21 bits total
        const int sdm_cur = (int)((numerator_target/f_xtal - 4)*65535 + .5);
        if ((sdm_cur < 0) || (sdm_cur > 2097151)) {
            continue;
        }

        const float numerator_cur = f_xtal*((sdm_cur/65535.0) + 4);
        const float f_achieved_cur = numerator_cur / denominator_cur;
        const float error_abs_cur = abs((f_achieved_cur - f_target)/f_target);

        if(!solved || (error_abs_cur < error_abs)) {
            *sdm0 = (sdm_cur & 0xFF);
            *sdm1 = ((sdm_cur >> 8) & 0xFF);
            *sdm2 = ((sdm_cur >> 16) & 0x1F);
            *odiv = odiv_cur;
            f_achieved = f_achieved_cur;
            error_abs = error_abs_cur;
            solved = true;
        }
    }

    if(solved) {
        ESP_LOGI(TAG, "APLL f_xtal:%i freq:%06f error:%03f sdm0:%i sdm1:%i sdm2:%i odiv:%i",
                f_xtal,
                f_achieved,
                error_abs,
                *sdm0,
                *sdm1,
                *sdm2,
                *odiv);
    }
    else {
        ESP_LOGI(TAG, "APLL failed to find setting");
    }

    return solved;
}


esp_err_t start_clock(float f_target) {
    int sdm0;
    int sdm1;
    int sdm2;
    int odir;

    // Note: The clock gets divided by 2 in the I2S module
    if(!calc_apll_setting(f_target*2, &sdm0, &sdm1, &sdm2, &odir)) {
        ESP_LOGE(TAG, "Error calculating APLL configuration");
        return ESP_FAIL;
    }

    // Configure the APLL for output
    rtc_clk_apll_enable(true, sdm0, sdm1, sdm2, odir);

    // Enable power to the I2S peripheral (?)
    periph_module_enable(i2s_periph_signal[0].module);

    // Configure the I2S peripheral to use APLL as a clock source
    REG_SET_FIELD(I2S_CLKM_CONF_REG(0), I2S_CLK_SEL, I2S_CLK_AUDIO_PLL);
    REG_SET_FIELD(I2S_CLKM_CONF_REG(0), I2S_CLKM_DIV_A, 0);
    REG_SET_FIELD(I2S_CLKM_CONF_REG(0), I2S_CLKM_DIV_B, 0);
    REG_SET_FIELD(I2S_CLKM_CONF_REG(0), I2S_CLKM_DIV_NUM, 1);
    REG_SET_FIELD(I2S_CLKM_CONF_REG(0), I2S_CLK_EN, 1);
    
    // Route the I2S clock to the CLK_OUT pins
    REG_WRITE(PIN_CTRL, 0);
    gpio_hal_iomux_func_sel(PERIPHS_IO_MUX_DAC_2_U, FUNC_DAC_2_CLK_OUT3);   // Output to FPGA clock buffer

    return ESP_OK;
}

// MAIN ///////////////////////////////////////////////////////////////////////

void app_main(void)
{
    status_led_init();
    status_led_set(true);

    ESP_ERROR_CHECK(fpga_start(&fpga_bin));

    start_clock(10.000);

    {
        vTaskDelay(1);
    }
}
