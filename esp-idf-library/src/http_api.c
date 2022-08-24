#include "http_api.h"
#include "http_response.h"
#include <esp_event.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <math.h>
#include <errno.h>

static const char* TAG = "http_api";

esp_err_t get_query_param(httpd_req_t* req, const char* name, char* buf, size_t buf_length)
{
    if((req == NULL) || (name == NULL) || (buf == NULL)) {
        return ESP_FAIL;
    }

    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    const size_t query_len = httpd_req_get_url_query_len(req) + 1;
    if (query_len == 1) {
        ESP_LOGE(TAG, "URL query missing");
        return ESP_FAIL;
    }

    char* query = malloc(query_len);
    if (httpd_req_get_url_query_str(req, query, query_len) != ESP_OK) {
        free(query);

        ESP_LOGE(TAG, "Error reading url query");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Found URL query => %s", query);
    /* Get value of expected key from query string */
    if (httpd_query_key_value(query, name, buf, buf_length) != ESP_OK) {
        free(query);

        ESP_LOGE(TAG, "Error reading address parameter");
        return ESP_FAIL;
    }

    free(query);
    return ESP_OK;
}

esp_err_t get_query_param_uint16(httpd_req_t* req, const char* name, uint16_t* val) {
    if(val == NULL) {
        return ESP_FAIL;
    }

    char value_s[10];
    esp_err_t ret = get_query_param(req, name, value_s, sizeof(value_s));
    if(ret != ESP_OK) {
        return ret;
    }

    errno = 0;

    char *end;
    const long int val_l = strtol(value_s, &end, 10);
    if((value_s == end) || (*end != '\0')) {
        ESP_LOGE(TAG, "Error reading integer");
        return ESP_FAIL;
    }

    if(errno != 0) {
        ESP_LOGE(TAG, "Error reading integer, errno:%i", errno);
        return ESP_FAIL;
    }

    if((val_l < 0) || (val_l > 0xFFFF)) {
     //   ESP_LOGE(TAG, "value out of range, value:%i", val_l);
        return ESP_FAIL;
    }

    *val = val_l;
    return ESP_OK;
}

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
    const esp_err_t err = ((http_api_json_put_callback_t)req->user_ctx)(req, request);

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
    const esp_err_t err = ((http_api_json_get_callback_t)req->user_ctx)(req, &response);

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
