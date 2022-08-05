#pragma once

#include <cJSON.h>
#include <esp_err.h>
#include <esp_http_server.h>

typedef esp_err_t (*http_api_json_put_callback_t)(const cJSON* request);
typedef esp_err_t (*http_api_json_get_callback_t)(cJSON** response);

typedef esp_err_t (*http_api_json_get_callback_bad_t)(const cJSON* request, cJSON** response);

esp_err_t http_api_register_json_put_endpoint(
    httpd_handle_t handle,
    const char* uri,
    http_api_json_put_callback_t callback
);

esp_err_t http_api_register_json_get_endpoint(
    httpd_handle_t handle,
    const char* uri,
    http_api_json_get_callback_t callback
);

esp_err_t http_api_register_json_get_endpoint_bad(
    httpd_handle_t handle,
    const char* uri,
    http_api_json_get_callback_bad_t callback
);

typedef esp_err_t (*http_api_binary_put_callback_t)(const char* buf, const int length);

esp_err_t http_api_register_binary_put_endpoint(
    httpd_handle_t handle,
    const char* uri,
    http_api_binary_put_callback_t callback
);

