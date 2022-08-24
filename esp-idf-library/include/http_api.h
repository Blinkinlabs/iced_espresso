#pragma once

#include <cJSON.h>
#include <esp_err.h>
#include <esp_http_server.h>

//! @brief Retrieve a url query parameter as a string
//!
//! @param[in] req HTTP request to search for parameter
//! @param[in] name parameter name
//! @param[out] buf Buffer to copy the parameter into
//! @param[in] buf_length Length of the buffer
//! @return ESP_OK on success
esp_err_t get_query_param(httpd_req_t* req, const char* name, char* buf, size_t buf_length);

//! @brief Retrieve a url query parameter as a uint16_t
//!
//! Read the parameter, converts it to an integer, and performs bounds checking.
//!
//! @param[in] req HTTP request to search for parameter
//! @param[in] name parameter name
//! @param[out] val parameter value
//! @return ESP_OK on success
esp_err_t get_query_param_uint16(httpd_req_t* req, const char* name, uint16_t* val);

typedef esp_err_t (*http_api_json_put_callback_t)(httpd_req_t* req, const cJSON* request);
typedef esp_err_t (*http_api_json_get_callback_t)(httpd_req_t* req, cJSON** response);

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

typedef esp_err_t (*http_api_binary_put_callback_t)(const char* buf, const int length);

esp_err_t http_api_register_binary_put_endpoint(
    httpd_handle_t handle,
    const char* uri,
    http_api_binary_put_callback_t callback
);

