#include "icedespresso_http_endpoint.h"
#include "icedespresso.h"
#include "http_api.h"
#include "ota_http_endpoint.h"
#include "fpga_http_endpoint.h"
#include <esp_log.h>

static const char* TAG = "ie_http_endpoint";

static esp_err_t http_status_led_put(httpd_req_t* req, const cJSON* json)
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

static esp_err_t http_status_led_get(httpd_req_t* req, cJSON** response)
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


void icedespresso_http_endpoints_register(httpd_handle_t httpd_handle) {
    ota_http_endpoint_register(httpd_handle, "/ota");
    http_api_register_json_put_endpoint(httpd_handle, "/status_led", http_status_led_put);
    http_api_register_json_get_endpoint(httpd_handle, "/status_led", http_status_led_get);

    fpga_http_endpoint_register(httpd_handle);
}
