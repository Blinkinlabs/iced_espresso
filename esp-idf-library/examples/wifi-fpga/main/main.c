#include "fpga.h"
#include "fpga_clock.h"
#include "http_api.h"
#include "ota.h"
#include "wifi_secrets.h"
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

// Button and status LED ////////////////////////////////////////////////////////////

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

//! @brief Returns true if the on-board button is pressed
bool button_pressed()
{
    return !gpio_get_level(BUTTON_PIN);
}

//! @brief Initialize the GPIO pin for the Status LED
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

//! @brief Enable or disable the blue status LED on the board
//!
//! @param val If true, turn on the LED; otherwise turn off the LED
esp_err_t status_led_set(const bool val)
{
    return gpio_set_level(STATUS_LED_PIN, !val);
}

//! @brief Get the value of the blue status LED
//!
//! @return true if the LED is on, false otherwise.
bool status_led_get()
{
    return !gpio_get_level(STATUS_LED_PIN);
}

// FPGA image ////////////////////////////////////////////////////////////////////////////

const uint8_t top_bin_start asm("_binary_top_bin_start");
const uint8_t top_bin_end asm("_binary_top_bin_end");

const fpga_bin_t fpga_bin = {
    .start = &top_bin_start,
    .end = &top_bin_end,
};

// Iced ESPresso REST API /////////////////////////////////////////////

esp_err_t http_status_led_put(const cJSON* json)
{
    if (json == NULL) {
        return ESP_FAIL;
    }

    const cJSON* state = cJSON_GetObjectItemCaseSensitive(json, "state");

    if (!cJSON_IsBool(state)) {
        ESP_LOGE(TAG, "Can't understand JSON");
        return ESP_FAIL;
    }

    return status_led_set(cJSON_IsTrue(state));
}

esp_err_t http_status_led_get(const cJSON* request, cJSON** response)
{
    // TODO: No request for this
    if (request == NULL) {
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "status_led_get");

    *response = cJSON_CreateObject();
    if (*response == NULL) {
        return ESP_FAIL;
    }

    cJSON* item;

    item = cJSON_CreateBool(status_led_get());
    if (item == NULL) {
        cJSON_Delete(*response);
        return ESP_FAIL;
    }
    cJSON_AddItemToObject(*response, "state", item);

    return ESP_OK;
}

//! @brief Check if the integer represents a valid register value
//!
//! The register address bus is 16-bit wide, so it can accept unsigned values
//! from 0x0000 to 0xFFFF.
//!
//! @param address Address to check
//! @return True if the address is valid, false otherwise
static bool register_validate_address(const int address) {
	return ((address >= 0x0000) && (address <= 0xFFFF));
}

//! @brief Check if the integer represents a valid register value
//!
//! The register data bus is 16-bit wide, so it can accept unsigned values
//! from 0x0000 to 0xFFFF.
//!
//! @param address Data value to check
//! @return True if the data is a valid register value, false otherwise
static bool register_validate_data(const int value) {
	return ((value >= 0x0000) && (value <= 0xFFFF));
}

esp_err_t register_put(const cJSON* request)
{
    if (request == NULL) {
        return ESP_FAIL;
    }

    const cJSON* address = cJSON_GetObjectItemCaseSensitive(request, "address");
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(request, "value");

    if (!cJSON_IsNumber(address)
        || !cJSON_IsNumber(value)) {
        ESP_LOGE(TAG, "Can't understand JSON");
        return ESP_FAIL;
    }

	if(!register_validate_address(address->valueint)) {
		ESP_LOGE(TAG, "Address out of range, address:%04x",
		address->valueint);
		return ESP_FAIL;
	}

	if(!register_validate_data(value->valueint)) {
		ESP_LOGE(TAG, "Value out of range, value:%04x",
		value->valueint);
		return ESP_FAIL;
	}

    ESP_LOGD(TAG, "register put, address:%04x value:%04x",
        address->valueint,
        value->valueint);

    return fpga_comms_write_register(address->valueint, value->valueint);
}

esp_err_t register_get(const cJSON* request, cJSON** response)
{
    if (request == NULL) {
        return ESP_FAIL;
    }

    const cJSON* address = cJSON_GetObjectItemCaseSensitive(request, "address");

    if (!cJSON_IsNumber(address)) {
        ESP_LOGE(TAG, "Can't understand JSON");
        return ESP_FAIL;
    }

	if(!register_validate_address(address->valueint)) {
		ESP_LOGE(TAG, "Address out of range, address:%04x",
		address->valueint);
		return ESP_FAIL;
	}

    ESP_LOGD(TAG, "register get, address:%04x",
        address->valueint);

    uint16_t val;
    esp_err_t ret = fpga_comms_read_register(address->valueint, &val);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error reading register");
        ;
        return ESP_FAIL;
    }

    *response = cJSON_CreateObject();
    if (*response == NULL) {
        return ESP_FAIL;
    }

    cJSON* item;

    item = cJSON_CreateNumber(address->valueint);
    if (item == NULL) {
        cJSON_Delete(*response);
        return ESP_FAIL;
    }
    cJSON_AddItemToObject(*response, "address", item);

    item = cJSON_CreateNumber(val);
    if (item == NULL) {
        cJSON_Delete(*response);
        return ESP_FAIL;
    }
    cJSON_AddItemToObject(*response, "value", item);

    return ESP_OK;
}


// WIFI ///////////////////////////////////////////////////////////////

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static int s_retry_num = 0;

static void event_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < CONFIG_WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &event_handler,
        NULL,
        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &event_handler,
        NULL,
        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .capable = true,
                .required = false },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
            EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
            EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

#define DMX_CHANNEL_COUNT 512
esp_err_t dmx_put(const char* buf, int length)
{

    if ((buf == NULL) || (length > DMX_CHANNEL_COUNT)) {
        return ESP_FAIL;
    }

//    uint8_t led_ram[DMX_CHANNEL_COUNT];
//
    ESP_LOGI(TAG, "put, length=%i", length);
    for (int led = 0; led < length; led++) {
        ESP_LOGI(TAG, "led:%i val:%i", led, buf[led]);
//        led_ram[led] = buf[led];
    }

    return fpga_comms_send_buffer(0x0000,
            (const uint8_t*)buf,
            length,
            0);
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

    fpga_clock_start(24);
    ESP_ERROR_CHECK(fpga_start(&fpga_bin));

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    ota_init();

    http_api_init();
    // Standard Iced Espresso endpoints:
    http_api_register_json_put_endpoint("/status_led", http_status_led_put);
    http_api_register_json_get_endpoint("/status_led", http_status_led_get);

// These are implemented in the http_api code
//    http_api_register_json_put_endpoint("/firmware", xxx);
//    http_api_register_json_put_endpoint("/fpga/bitstream", bitsream_put);

    http_api_register_json_put_endpoint("/fpga/register", register_put);
    http_api_register_json_get_endpoint("/fpga/register", register_get);
    //http_api_register_json_put_endpoint("/fpga/memory/*", memory_put);
    //http_api_register_json_get_endpoint("/fpga/memory/*", memory_get);
    
    http_api_register_binary_put_endpoint("/dmx", dmx_put);

    while (true) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        if (button_pressed()) {
            ESP_LOGI(TAG, "stopping wifi, then pausing for reset...");
            esp_wifi_stop();
            while (true) {
                vTaskDelay(1000 / portTICK_PERIOD_MS);
            }
        }
    }
}
