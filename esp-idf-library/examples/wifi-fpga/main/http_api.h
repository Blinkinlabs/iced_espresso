#pragma once

#include <cJSON.h>
#include <esp_err.h>
#include <stdbool.h>

//! @brief Get a string ID from a uri string
//!
//! Example: URI is "/listeners/sacn", header is "/listeners/", then return is "sacn"
//!
//! @param[in] uri String containing the request URI
//! @param[in] header String containing the base URI, to be filtered
//! @param[out] id Pointer to memory location to store string ID, if successful
//! @param[in] id_size Length of string buffer avaible to write id
//! @return true if ID was successfully extracted
bool get_string_id_from_uri(const char* uri, const char* header, char* id, int id_size);

//! @brief Get an integer ID from a uri string
//!
//! Example: URI is "/mappings/1234", uri_len is 15, header is "/mappings/", then return is 1234
//!
//! @param[in] uri String containing the request URI
//! @param[in] header String containing the base URI, to be filtered outputs:
//! @param[out] id Pointer to memory location to store ID, if successful
//! @return true if ID was successfully extracted
bool get_int_id_from_uri(const char* uri, const char* header, int* id); 

void http_api_init();

typedef esp_err_t (*http_api_json_put_callback_t)(const cJSON* request);
typedef esp_err_t (*http_api_json_get_callback_t)(const cJSON* request, cJSON** response);

esp_err_t http_api_register_json_put_endpoint(
    const char* uri,
    http_api_json_put_callback_t callback);

esp_err_t http_api_register_json_get_endpoint(
    const char* uri,
    http_api_json_get_callback_t callback);

typedef esp_err_t (*http_api_binary_put_callback_t)(const char* buf, const int length);

esp_err_t http_api_register_binary_put_endpoint(
    const char* uri,
    http_api_binary_put_callback_t callback);
