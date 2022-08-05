#include "icedespresso_http_endpoint.h"
#include "icedespresso.h"
#include "http_api.h"
#include "fpga.h"
#include "ota_http_endpoint.h"
#include <cJSON.h>
#include <esp_log.h>
#include <esp_err.h>


static const char* TAG = "ie_http_endpoint";


static esp_err_t http_status_led_put(const cJSON* json)
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

static esp_err_t http_status_led_get(cJSON** response)
{
    *response = cJSON_CreateObject();
    if (*response == NULL) {
        return ESP_FAIL;
    }

    cJSON* state;

    state = cJSON_CreateBool(status_led_get());
    if (state == NULL) {
        cJSON_Delete(*response);
        return ESP_FAIL;
    }
    cJSON_AddItemToObject(*response, "state", state);

    return ESP_OK;
}

static esp_err_t register_put(const cJSON* request)
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

    ESP_LOGD(TAG, "register put, address:%04x value:%04x",
        address->valueint,
        value->valueint);

    return fpga_comms_write_register(address->valueint, value->valueint);
}

static esp_err_t register_get(const cJSON* request, cJSON** response)
{
    if (request == NULL) {
        return ESP_FAIL;
    }

    const cJSON* address = cJSON_GetObjectItemCaseSensitive(request, "address");

    if (!cJSON_IsNumber(address)) {
        ESP_LOGE(TAG, "Can't understand JSON");
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

    cJSON* item = cJSON_CreateNumber(val);
    if (item == NULL) {
        cJSON_Delete(*response);
        return ESP_FAIL;
    }
    cJSON_AddItemToObject(*response, "value", item);

    return ESP_OK;
}

void icedespresso_http_endpoints_register(httpd_handle_t httpd_handle) {
    ota_http_endpoint_register(httpd_handle, "/ota");
    http_api_register_json_put_endpoint(httpd_handle, "/status_led", http_status_led_put);
    http_api_register_json_get_endpoint(httpd_handle, "/status_led", http_status_led_get);
    //http_api_register_json_put_endpoint(httpd_handle, "/fpga/bitstream", bitsream_put);
    http_api_register_json_put_endpoint(httpd_handle, "/fpga/register", register_put);
    http_api_register_json_get_endpoint_bad(httpd_handle, "/fpga/register", register_get);
    //http_api_register_json_put_endpoint(httpd_handle, "/fpga/memory", memory_put);
    //http_api_register_json_get_endpoint(httpd_handle, "/fpga/memory", memory_get);
}
