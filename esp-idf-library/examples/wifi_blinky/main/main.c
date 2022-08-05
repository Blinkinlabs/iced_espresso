/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "icedespresso.h"
#include "icedespresso_http_endpoint.h"
#include "ota.h"
#include "http_api.h"
#include "fpga.h"

#include "wifi_secrets.h"
#include <cJSON.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <lwip/err.h>
#include <lwip/sys.h>
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


static esp_err_t rgb_led_put(const cJSON* json)
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

// main /////////////////////////////////////////////////////////////////////////////

static httpd_handle_t httpd_handle = NULL;

void http_init()
{

//    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
//    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&httpd_handle, &config) != ESP_OK) {
        ESP_LOGI(TAG, "Error starting http server!");
        return;
    }

    // Standard Iced Espresso endpoints
    icedespresso_http_endpoints_register(httpd_handle);

    http_api_register_json_put_endpoint(httpd_handle, "/rgb_led", rgb_led_put);
}

//static void connect_handler(void* arg, esp_event_base_t event_base,
//    int32_t event_id, void* event_data)
//{
//    httpd_handle_t* httpd_handle = (httpd_handle_t*)arg;
//    if (*httpd_handle == NULL) {
//        ESP_LOGI(TAG, "Starting webserver");
//        *httpd_handle = start_webserver();
//    }
//}
//
//static void disconnect_handler(void* arg, esp_event_base_t event_base,
//    int32_t event_id, void* event_data)
//{
//    httpd_handle_t* httpd_handle = (httpd_handle_t*)arg;
//    if (*httpd_handle) {
//        ESP_LOGI(TAG, "Stopping webserver");
//        stop_webserver(*httpd_handle);
//        *httpd_handle = NULL;
//    }
//}

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

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    ota_init();

    http_init();

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
