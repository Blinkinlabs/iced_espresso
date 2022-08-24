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

    fpga_comms_register_write(RED_DUTY_REG, red_i);
    fpga_comms_register_write(GREEN_DUTY_REG, green_i);
    return fpga_comms_register_write(BLUE_DUTY_REG, blue_i);
}

esp_err_t led_get(
    double* red,
    double* green,
    double* blue)
{
    uint16_t red_uint;
    uint16_t green_uint;
    uint16_t blue_uint;

    fpga_comms_register_read(RED_DUTY_REG, &red_uint);
    fpga_comms_register_read(GREEN_DUTY_REG, &green_uint);
    fpga_comms_register_read(BLUE_DUTY_REG, &blue_uint);

    *red = red_uint/65535.0;
    *green = green_uint/65535.0;
    *blue = blue_uint/65535.0;

    return ESP_OK;
}



// CM-2 /////////////////////////////////////////////////////////////////////////////
float distance(float x1, float y1, float x2, float y2)
{
    const float total = (x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2);
    return sqrt(total);
}

// Number of LEDs in a single panel
#define LED_ROWS 32
#define LED_COLS 8
#define LED_COUNT (LED_ROWS*LED_COLS)

static double g_brightness = 1;

static void display()
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
            led_ram_left[i] = (int)(g_brightness*(127 * (sin(phase - dist_left / 2.0) + 1)));

            const float dist_right = distance(x + 13, y * .65, x_focus, y_focus * .65);
            led_ram_right[i] = (int)(g_brightness*(127 * (sin(phase - dist_right / 2.0) + 1)));
        }
    }

    fpga_comms_memory_write(0x0200, (const uint8_t*)led_ram_left, sizeof(led_ram_left), 0);
    fpga_comms_memory_write(0x0000, (const uint8_t*)led_ram_right, sizeof(led_ram_right), 0);

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

static esp_err_t brightness_put(httpd_req_t* req, const cJSON* request)
{
    if (request == NULL) {
        return ESP_FAIL;
    }

    const cJSON* brightness = cJSON_GetObjectItemCaseSensitive(request, "brightness");

    if (!cJSON_IsNumber(brightness)) {
        ESP_LOGE(TAG, "Can't understand JSON");
        return ESP_FAIL;
    }

    if ((brightness->valuedouble > 1) || (brightness->valuedouble < 0)) {
        ESP_LOGE(TAG, "brightness out of range, brightness:%f",
            brightness->valuedouble);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "brightness post, brightness:%f",
        brightness->valuedouble);

    g_brightness = brightness->valuedouble;

    return ESP_OK;
}

// CM-2 HTTP endpoints ////////////////////////////////////////////////////

static esp_err_t rgb_led_put(httpd_req_t* req, const cJSON* json)
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

    if ((red->valuedouble > 1) || (red->valuedouble < 0)
        || (green->valuedouble > 1) || (green->valuedouble < 0)
        || (blue->valuedouble > 1) || (blue->valuedouble < 0)
        ) {
        ESP_LOGE(TAG, "color data out of range, red:%f green:%f blue:%f",
            red->valuedouble,
            green->valuedouble,
            blue->valuedouble
            );
        return ESP_FAIL;
    }

    return led_set(red->valuedouble,
        green->valuedouble,
        blue->valuedouble);
}

static esp_err_t rgb_led_get(httpd_req_t* req, cJSON** response)
{
    double red_d;
    double green_d;
    double blue_d;
    led_get(&red_d, &green_d, &blue_d);

    *response = cJSON_CreateObject();
    if (*response == NULL) {
        return ESP_FAIL;
    }

    cJSON* red = cJSON_CreateNumber(red_d);
    if (red == NULL) {
        cJSON_Delete(*response);
        return ESP_FAIL;
    }
    cJSON_AddItemToObject(*response, "red", red);

    cJSON* green = cJSON_CreateNumber(green_d);
    if (green == NULL) {
        cJSON_Delete(*response);
        return ESP_FAIL;
    }
    cJSON_AddItemToObject(*response, "green", green);

    cJSON* blue = cJSON_CreateNumber(blue_d);
    if (blue == NULL) {
        cJSON_Delete(*response);
        return ESP_FAIL;
    }
    cJSON_AddItemToObject(*response, "blue", blue);

    return ESP_OK;
}

static esp_err_t brightness_get(httpd_req_t* req, cJSON** response)
{
    *response = cJSON_CreateObject();
    if (*response == NULL) {
        return ESP_FAIL;
    }

    cJSON* brightness;

    brightness = cJSON_CreateNumber(g_brightness);
    if (brightness == NULL) {
        cJSON_Delete(*response);
        return ESP_FAIL;
    }
    cJSON_AddItemToObject(*response, "brightness", brightness);

    return ESP_OK;
}

// TODO: Make this a message / local variable
bool wifi_mode;

static esp_err_t bitmap_put(const char* buf, int length)
{

    if ((buf == NULL) || (length != LED_COUNT*2)) {
        return ESP_FAIL;
    }

    wifi_mode = true;

    uint16_t led_ram_left[LED_COUNT];
    uint16_t led_ram_right[LED_COUNT];

    for (int led = 0; led < LED_COUNT; led++) {
        const int col = led % LED_COLS;
        const int row = led / LED_COLS;
        
        // TODO: Fix the buffer direction in the FPGA
        led_ram_left[led] = buf[row*LED_COLS*2 + (LED_COLS*2-1-col)];
        led_ram_right[led] = buf[(row*LED_COLS*2 + (LED_COLS*2-1-8-col))];
    }

    fpga_comms_memory_write(0x0200, (const uint8_t*)led_ram_left, sizeof(led_ram_left), 0);
    fpga_comms_memory_write(0x0000, (const uint8_t*)led_ram_right, sizeof(led_ram_right), 0);

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
    http_api_register_json_get_endpoint(httpd_handle, "/rgb_led", rgb_led_get);
    http_api_register_json_put_endpoint(httpd_handle, "/brightness", brightness_put);
    http_api_register_json_get_endpoint(httpd_handle, "/brightness", brightness_get);
    http_api_register_binary_put_endpoint(httpd_handle, "/bitmap", bitmap_put);
}

#define WIFI_RESET_TIME_S 3

static void check_button(void) {
    static uint64_t start_time = 0;
    static bool last_pressed = false;

    if(button_pressed()) {
        if (!last_pressed) {
            last_pressed = true;
            start_time = esp_timer_get_time();
        }
        else {
            if(esp_timer_get_time() - start_time > (uint64_t) WIFI_RESET_TIME_S*1000000) {
                last_pressed = false;
                ESP_LOGI(TAG, "Resetting wifi connection");
                wifi_manager_disconnect_async();
            }
        }
    }
    else {
        last_pressed = false;
    }
}

// main /////////////////////////////////////////////////////////////////////////////
void app_main(void)
{

    button_init();
    status_led_init();


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
        check_button();

        if(!wifi_mode) {
            display();
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
