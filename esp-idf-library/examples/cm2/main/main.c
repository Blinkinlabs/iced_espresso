#include "icedespresso.h"
#include "icedespresso_http_endpoint.h"
#include "ota.h"
#include "http_api.h"
#include "fpga.h"

#include "wifi_manager.h"
#include "http_app.h"

#include <cJSON.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <lwip/err.h>
#include <lwip/sys.h>
#include <math.h>
#include <nvs_flash.h>
#include <sys/param.h>


static const char* TAG = "blinky";

// FPGA image ////////////////////////////////////////////////////////////////////////////

const uint8_t top_bin_start asm("_binary_top_bin_start");
const uint8_t top_bin_end asm("_binary_top_bin_end");

const fpga_bin_t fpga_bin = {
    .start = &top_bin_start,
    .end = &top_bin_end,
};

// FPGA Interface ////////////////////////////////////////////////////////////////////////

#define RED_DUTY_REG 0x00F0
#define GREEN_DUTY_REG 0x00F1
#define BLUE_DUTY_REG 0x00F2

esp_err_t led_set(
    double red,
    double green,
    double blue)
{
    ESP_LOGD(TAG, "led_set, r:%f g:%f b:%f", red, green, blue);

    const uint16_t red_i = red * 65535;
    const uint16_t green_i = green * 65535;
    const uint16_t blue_i = blue * 65535;

    fpga_comms_write_register(RED_DUTY_REG, red_i);
    fpga_comms_write_register(GREEN_DUTY_REG, green_i);
    return fpga_comms_write_register(BLUE_DUTY_REG, blue_i);
}

// HTTP server ////////////////////////////////////////////////////////

esp_err_t rgb_led_put(const cJSON* json)
{
    if (json == NULL) {
        return ESP_FAIL;
    }

    const cJSON* red = cJSON_GetObjectItemCaseSensitive(json, "red");
    const cJSON* green = cJSON_GetObjectItemCaseSensitive(json, "green");
    const cJSON* blue = cJSON_GetObjectItemCaseSensitive(json, "blue");

    if (!cJSON_IsNumber(red)
        || !cJSON_IsNumber(green)
        || !cJSON_IsNumber(blue)) {
        ESP_LOGE(TAG, "Can't understand JSON");
        return ESP_FAIL;
    }

    return led_set(red->valuedouble,
        green->valuedouble,
        blue->valuedouble);
}



// CM-2 /////////////////////////////////////////////////////////////////////////////
float distance(float x1, float y1, float x2, float y2)
{
    const float total = (x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2);
    return sqrt(total);
}

#define LED_COUNT 256

void display()
{
    static uint16_t led_ram_left[LED_COUNT];
    static uint16_t led_ram_right[LED_COUNT];

    static float phase = 0;

    static float x_focus = 3;
    static float y_focus = 15;
    static bool x_focus_dir = true;
    static bool y_focus_dir = true;

    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 32; y++) {
            const int i = x + y * 8;

            const float dist_left = distance(x, y * .65, x_focus, y_focus * .65);
            led_ram_left[i] = (int)((127 * (sin(phase - dist_left / 2.0) + 1)));

            const float dist_right = distance(x + 13, y * .65, x_focus, y_focus * .65);
            led_ram_right[i] = (int)((127 * (sin(phase - dist_right / 2.0) + 1)));
        }
    }

    fpga_comms_send_buffer(0x0200, (const uint8_t*)led_ram_left, sizeof(led_ram_left), 0);
    fpga_comms_send_buffer(0x0000, (const uint8_t*)led_ram_right, sizeof(led_ram_right), 0);

    phase += .1;

    const float x_focus_speed = .02;
    const float y_focus_speed = .03;

    if (x_focus_dir) {
        x_focus += x_focus_speed;
        if (x_focus > 21 + 3)
            x_focus_dir = !x_focus_dir;
    } else {
        x_focus -= x_focus_speed;
        if (x_focus < 0 - 3)
            x_focus_dir = !x_focus_dir;
    }

    if (y_focus_dir) {
        y_focus += y_focus_speed;
        if (y_focus > 31)
            y_focus_dir = !y_focus_dir;
    } else {
        y_focus -= y_focus_speed;
        if (y_focus < 0)
            y_focus_dir = !y_focus_dir;
    }
}

esp_err_t brightness_put(const cJSON* request)
{
    if (request == NULL) {
        return ESP_FAIL;
    }

    const cJSON* brightness = cJSON_GetObjectItemCaseSensitive(request, "brightness");

    if (!cJSON_IsNumber(brightness)) {
        ESP_LOGE(TAG, "Can't understand JSON");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "brightness post, brightness:%i",
        brightness->valueint);

    uint16_t led_ram[LED_COUNT];

    for (int led = 0; led < LED_COUNT; led++) {
        led_ram[led] = brightness->valueint;
    }

    fpga_comms_send_buffer(0x0200, (const uint8_t*)led_ram, sizeof(led_ram), 0);
    fpga_comms_send_buffer(0x0000, (const uint8_t*)led_ram, sizeof(led_ram), 0);

    return ESP_OK;
}

// TODO: Make this a message / local variable
bool wifi_mode;

esp_err_t bitmap_put(const char* buf, int length)
{

    if ((buf == NULL) || (length != LED_COUNT)) {
        return ESP_FAIL;
    }

    wifi_mode = true;

    uint16_t led_ram[LED_COUNT];

    for (int led = 0; led < LED_COUNT; led++) {
        led_ram[led] = buf[led];
    }

    fpga_comms_send_buffer(0x0200, (const uint8_t*)led_ram, sizeof(led_ram), 0);
    fpga_comms_send_buffer(0x0000, (const uint8_t*)led_ram, sizeof(led_ram), 0);

    return ESP_OK;
}


/**
 * @brief this is an exemple of a callback that you can setup in your own app to get notified of wifi manager event.
 */
static void cb_connection_ok(void *pvParameter){
	ip_event_got_ip_t* param = (ip_event_got_ip_t*)pvParameter;

	/* transform IP to human readable string */
	char str_ip[16];
	esp_ip4addr_ntoa(&param->ip_info.ip, str_ip, IP4ADDR_STRLEN_MAX);

	ESP_LOGI(TAG, "I have a connection and my IP is %s!", str_ip);
}

static void uri_callback(httpd_handle_t httpd_handle) {
    ESP_LOGI(TAG, "HTTP server started, register endpoints?");

    // Standard Iced Espresso endpoints
    icedespresso_http_endpoints_register(httpd_handle);

    //CM-2 specific endpoints
    http_api_register_json_put_endpoint(httpd_handle, "/rgb_led", rgb_led_put);
    http_api_register_json_put_endpoint(httpd_handle, "/brightness", brightness_put);
    http_api_register_binary_put_endpoint(httpd_handle, "/bitmap", bitmap_put);
}

// main /////////////////////////////////////////////////////////////////////////////
void app_main(void)
{

    button_init();
    status_led_init();

    // Delay for a while, to allow resetting into bootloader mode
    for (int i = 3; i > 0; i--) {
        ESP_LOGI(TAG, "Starting in %is...", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    if (button_pressed()) {
        ESP_LOGI(TAG, "Pausing for reset...");
        while (true) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    ESP_ERROR_CHECK(fpga_start(&fpga_bin));

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ota_init();

    http_app_set_uri_callback(&uri_callback);

	/* start the wifi manager */
	wifi_manager_start();

	/* register a callback as an example to how you can integrate your code with the wifi manager */
	wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &cb_connection_ok);


    wifi_mode = false;
    while (true) {
        if(!wifi_mode) {
            display();
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
