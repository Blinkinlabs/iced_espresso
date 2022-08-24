#include <esp_http_server.h>

//! @defgroup api_utils REST API utility functions
//!
//! @brief Functions for presenting a unified JSON REST API over HTTP/HTTPS
//!
//! @{

// TODO: Handle all error codes
#define RESPOND_ERROR(response_code, error_text, ...)                                     \
    {                                                                                     \
        char err_msg[100];                                                                \
        int pos = 0;                                                                      \
        pos += snprintf(err_msg + pos, sizeof(err_msg) - pos, "{\"error\":\"");           \
        pos += snprintf(err_msg + pos, sizeof(err_msg) - pos, error_text, ##__VA_ARGS__); \
        pos += snprintf(err_msg + pos, sizeof(err_msg) - pos, "\"}");                     \
        ESP_LOGE(TAG, error_text, ##__VA_ARGS__);                                         \
        httpd_resp_set_type(req, "application/json");                                     \
        switch (response_code) {                                                          \
        case HTTPD_400_BAD_REQUEST:                                                       \
            httpd_resp_set_status(req, "400 Bad Request");                                \
            break;                                                                        \
        case HTTPD_404_NOT_FOUND:                                                         \
            httpd_resp_set_status(req, "404 Not Found");                                  \
            break;                                                                        \
        case HTTPD_500_INTERNAL_SERVER_ERROR:                                             \
        default:                                                                          \
            httpd_resp_set_status(req, "500 Internal Server Error");                      \
        }                                                                                 \
        httpd_resp_send(req, err_msg, strlen(err_msg));                                   \
    }

#define RESPOND_ERROR_PARSING_ID() \
    RESPOND_ERROR(HTTPD_404_NOT_FOUND, "Could not determine ID from URI");

#define RESPOND_ERROR_INVALID_ID(id) \
    RESPOND_ERROR(HTTPD_404_NOT_FOUND, "Invalid id:%s", id);

#define RESPOND_ERROR_INVALID_ID_INT(id) \
    RESPOND_ERROR(HTTPD_404_NOT_FOUND, "Invalid id:%i", id);

// Note: HTTP library doesn't support sending HTTPD_413_PAYLOAD_TOO_LARGE
#define RESPOND_ERROR_PAYLOAD_TOO_BIG(payload_length, max_length) \
    RESPOND_ERROR(HTTPD_400_BAD_REQUEST,                          \
        "Request too big request_size:%i, buffer_size:%zu",       \
        payload_length, max_length);

#define RESPOND_ERROR_RECEIVING_DATA() \
    RESPOND_ERROR(HTTPD_500_INTERNAL_SERVER_ERROR, "HTTPD read failure");

#define RESPOND_ERROR_TRANSMITTING_DATA() \
    RESPOND_ERROR(HTTPD_500_INTERNAL_SERVER_ERROR, "HTTPD write failure");

#define RESPOND_ERROR_PARSING_JSON() \
    RESPOND_ERROR(HTTPD_400_BAD_REQUEST, "Error parsing JSON");

#define RESPOND_ERROR_INVALID_PARAMETERS() \
    RESPOND_ERROR(HTTPD_400_BAD_REQUEST, "Error reading parameters");

#define RESPOND_ERROR_APPLYING_STATE() \
    RESPOND_ERROR(HTTPD_400_BAD_REQUEST, "Error applying state");

#define RESPOND_ERROR_SAVING_STATE() \
    RESPOND_ERROR(HTTPD_400_BAD_REQUEST, "Error saving configuration file");

#define RESPOND_OK()                              \
    httpd_resp_set_type(req, "application/json"); \
    httpd_resp_sendstr(req, "{\"code\":0, \"message\":\"Ok\"}");
