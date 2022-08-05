#include "http_api.h"
#include "http_response.h"
#include <esp_event.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <math.h>

static const char* TAG = "http_api";

static esp_err_t binary_put_handler(httpd_req_t* req)
{
    // Handle messages up to 600 bytes
    char buf[600];

    // Contents are too long, bail
    if (req->content_len > sizeof(buf)) {
        // TODO: Send HTTP fail response
        RESPOND_ERROR_PAYLOAD_TOO_BIG(req->content_len, sizeof(buf));
        return ESP_FAIL;
    }

    /* Read the data for the request */
    const int ret = httpd_req_recv(req, buf, req->content_len);

    if (ret <= 0) {
        RESPOND_ERROR_RECEIVING_DATA();
        return ESP_FAIL;
    }

    if (ret != req->content_len) {
        RESPOND_ERROR_RECEIVING_DATA();
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "=========== RECEIVED DATA ==========");
    ESP_LOGD(TAG, "%.*s", ret, buf);
    ESP_LOGD(TAG, "====================================");

    // Pass it to the context
    const esp_err_t err = ((http_api_binary_put_callback_t)req->user_ctx)(buf, req->content_len);

    // End response
    if (err != ESP_OK) {
        RESPOND_ERROR_APPLYING_STATE();
        return err;
    }

    RESPOND_OK();
    return ESP_OK;
}

static esp_err_t json_put_handler(httpd_req_t* req)
{
    // Handle messages up to 600 bytes
    char buf[600];

    // Contents are too long, bail
    if (req->content_len > sizeof(buf)) {
        // TODO: Send HTTP fail response
        RESPOND_ERROR_PAYLOAD_TOO_BIG(req->content_len, sizeof(buf));
        return ESP_FAIL;
    }

    /* Read the data for the request */
    const int ret = httpd_req_recv(req, buf, req->content_len);

    if (ret <= 0) {
        RESPOND_ERROR_RECEIVING_DATA();
        return ESP_FAIL;
    }

    if (ret != req->content_len) {
        RESPOND_ERROR_RECEIVING_DATA();
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "=========== RECEIVED DATA ==========");
    ESP_LOGD(TAG, "%.*s", ret, buf);
    ESP_LOGD(TAG, "====================================");

    // Parse the message data as JSON
    cJSON* request = cJSON_ParseWithLength(buf, ret);
    if (request == NULL) {
        const char* error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "Error before: %s", error_ptr);
            RESPOND_ERROR_PARSING_JSON();
            return ESP_FAIL;
        }

        RESPOND_ERROR_PARSING_JSON();
        return ESP_FAIL;
    }

    // Pass it to the context
    const esp_err_t err = ((http_api_json_put_callback_t)req->user_ctx)(request);

    cJSON_Delete(request);

    // End response
    if (err != ESP_OK) {
        RESPOND_ERROR_APPLYING_STATE();
        return err;
    }

    RESPOND_OK();
    return ESP_OK;
}

static esp_err_t json_get_handler(httpd_req_t* req)
{
    // Handle messages up to 600 bytes
    char buf[600];


    // Pass it to the context
    cJSON* response = NULL;
    const esp_err_t err = ((http_api_json_get_callback_t)req->user_ctx)(&response);

    // End response
    if (err != ESP_OK) {
        RESPOND_ERROR_APPLYING_STATE();
        return err;
    }

    // TODO:Pack message and send response
    cJSON_PrintPreallocated(response, buf, sizeof(buf), true);
    cJSON_Delete(response);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);

    return ESP_OK;
}

static esp_err_t json_get_handler_bad(httpd_req_t* req)
{
    // Handle messages up to 600 bytes
    char buf[600];

    // Contents are too long, bail
    if (req->content_len > sizeof(buf)) {
        // TODO: Send HTTP fail response
        RESPOND_ERROR_PAYLOAD_TOO_BIG(req->content_len, sizeof(buf));
        return ESP_FAIL;
    }

    /* Read the data for the request */
    const int ret = httpd_req_recv(req, buf, req->content_len);

    if (ret <= 0) {
        RESPOND_ERROR_RECEIVING_DATA();
        return ESP_FAIL;
    }

    if (ret != req->content_len) {
        RESPOND_ERROR_RECEIVING_DATA();
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "=========== RECEIVED DATA ==========");
    ESP_LOGD(TAG, "%.*s", ret, buf);
    ESP_LOGD(TAG, "====================================");

    // Parse the message data as JSON
    cJSON* request = cJSON_ParseWithLength(buf, ret);

    if (request == NULL) {
        const char* error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "Error before: %s", error_ptr);
            RESPOND_ERROR_PARSING_JSON();
            return ESP_FAIL;
        }

        RESPOND_ERROR_PARSING_JSON();
        return ESP_FAIL;
    }

    // Pass it to the context
    cJSON* response = NULL;
    const esp_err_t err = ((http_api_json_get_callback_bad_t)req->user_ctx)(request, &response);

    cJSON_Delete(request);

    // End response
    if (err != ESP_OK) {
        RESPOND_ERROR_APPLYING_STATE();
        return err;
    }

    // TODO:Pack message and send response
    cJSON_PrintPreallocated(response, buf, sizeof(buf), true);
    cJSON_Delete(response);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);

    return ESP_OK;
}

esp_err_t http_api_register_json_put_endpoint(
    httpd_handle_t handle,
    const char* uri,
    http_api_json_put_callback_t callback)
{
    if (handle == NULL) {
        return ESP_FAIL;
    }

    const httpd_uri_t httpd_uri = {
        .uri = uri,
        .method = HTTP_PUT,
        .handler = json_put_handler,
        .user_ctx = callback
    };

    return httpd_register_uri_handler(handle, &httpd_uri);
}

esp_err_t http_api_register_json_get_endpoint(
    httpd_handle_t handle,
    const char* uri,
    http_api_json_get_callback_t callback
)
{
    if (handle == NULL) {
        return ESP_FAIL;
    }

    const httpd_uri_t httpd_uri = {
        .uri = uri,
        .method = HTTP_GET,
        .handler = json_get_handler,
        .user_ctx = callback
    };

    // TODO: Do these persist over a stop/start call?
    return httpd_register_uri_handler(handle, &httpd_uri);
}

esp_err_t http_api_register_json_get_endpoint_bad(
    httpd_handle_t handle,
    const char* uri,
    http_api_json_get_callback_bad_t callback
)
{
    if (handle == NULL) {
        return ESP_FAIL;
    }

    const httpd_uri_t httpd_uri = {
        .uri = uri,
        .method = HTTP_GET,
        .handler = json_get_handler_bad,
        .user_ctx = callback
    };

    // TODO: Do these persist over a stop/start call?
    return httpd_register_uri_handler(handle, &httpd_uri);
}

esp_err_t http_api_register_binary_put_endpoint(
    httpd_handle_t handle,
    const char* uri,
    http_api_binary_put_callback_t callback)
{
    if (handle == NULL) {
        return ESP_FAIL;
    }

    const httpd_uri_t httpd_uri = {
        .uri = uri,
        .method = HTTP_PUT,
        .handler = binary_put_handler,
        .user_ctx = callback
    };

    // TODO: Do these persist over a stop/start call?
    return httpd_register_uri_handler(handle, &httpd_uri);
}
