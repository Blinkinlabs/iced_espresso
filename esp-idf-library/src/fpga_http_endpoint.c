#include "fpga_http_endpoint.h"
#include "fpga_loader.h"
#include "http_response.h"
#include "http_api.h"
#include "fpga.h"
#include <esp_log.h>

static const char* TAG = "fpga_http_endpoint";

// Must be equal to or smaller than FPGA buffer size
#define CHUNK_SIZE (512)

static esp_err_t bitstream_put_handler(httpd_req_t* req)
{
    esp_err_t ret;

    char* buf = malloc(CHUNK_SIZE);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Unable to reserve memory for FPGA loading");
        RESPOND_ERROR_RECEIVING_DATA();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Starting FPGA load, length:%i", req->content_len);
    ret = fpga_loader_start();

    size_t remaining = req->content_len;

    while (remaining > 0) {
        const size_t chunk_size = ((CHUNK_SIZE < remaining) ? CHUNK_SIZE : remaining);

        /* Read the data for the request */
        const int received = httpd_req_recv(req, buf, chunk_size);
        ESP_LOGI(TAG, "FPGA load, chunk_size:%i remaining:%i", chunk_size, remaining);

        if (received <= 0) {
            ESP_LOGE(TAG, "FPGA load error receiving data, received:%i", received);
            fpga_loader_abort();
            RESPOND_ERROR_RECEIVING_DATA();
            free(buf);
            return ESP_FAIL;
        }

        if (received != chunk_size) {
            ESP_LOGE(TAG, "FPGA load too little data received, received:%i", received);
        }

        ret = fpga_loader_add_chunk(buf, received);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "FPGA load error adding chunk, ret:%i", ret);
            fpga_loader_abort();
            RESPOND_ERROR_RECEIVING_DATA();
            free(buf);
            return ESP_FAIL;
        }

        remaining -= received;
    }

    free(buf);

    ESP_LOGI(TAG, "Finishing FPGA load");
    ret = fpga_loader_finalize();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error performing FPGA load");
        RESPOND_ERROR_APPLYING_STATE();
        return ret;
    }

    ESP_LOGI(TAG, "FPGA load successful");
    RESPOND_OK();

    return ESP_OK;
}

static esp_err_t register_put(httpd_req_t* req, const cJSON* request)
{
    uint16_t address;
    esp_err_t ret = get_query_param_uint16(req, "address", &address);
    if(ret != ESP_OK) {
        return ret;
    }

    if (request == NULL) {
        return ESP_FAIL;
    }
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(request, "value");

    if (!cJSON_IsNumber(value)) {
        ESP_LOGE(TAG, "Can't understand JSON");
        return ESP_FAIL;
    }
    if ((value->valueint < 0) || (value->valueint > 0xFFFF)) {
        ESP_LOGE(TAG, "Value out of range, value:%i", value->valueint);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "register put, address:%04x value:%04x",
        address,
        value->valueint);

    return fpga_comms_register_write(address, value->valueint);
}

static esp_err_t register_get(httpd_req_t* req, cJSON** response)
{
    uint16_t address;
    esp_err_t ret = get_query_param_uint16(req, "address", &address);
    if(ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "register_get, address:0x%04x", address);

    uint16_t val;
    ret = fpga_comms_register_read(address, &val);
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

static esp_err_t memory_put_handler(httpd_req_t* req)
{
    esp_err_t ret;

    uint16_t address;
    ret = get_query_param_uint16(req, "address", &address);
    if(ret != ESP_OK) {
        RESPOND_ERROR_INVALID_PARAMETERS();
        return ret;
    }

    size_t length = req->content_len;

    if(length > CHUNK_SIZE) {
        RESPOND_ERROR_INVALID_PARAMETERS();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "memory write, address:0x%04x length:%i", address, length);

    char* buf = malloc(length);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Unable to reserve memory for memory put");
        RESPOND_ERROR_RECEIVING_DATA();
        return ESP_FAIL;
    }

    size_t remaining = length;
    while (remaining > 0) {
        const size_t offset = length - remaining;

        /* Read the data for the request */
        const int received = httpd_req_recv(req, buf + offset, remaining);
        ESP_LOGD(TAG, "memory write, received:%i remaining:%i", received, remaining);

        if (received <= 0) {
            RESPOND_ERROR_RECEIVING_DATA();
            free(buf);
            return ESP_FAIL;
        }

        remaining -= received;
    }
    
    ret = fpga_comms_memory_write(address, (uint8_t*)buf, length, 0);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "Error writing to FPGA memory");
        RESPOND_ERROR_SAVING_STATE();
        free(buf);
        return ESP_FAIL;
    }

    free(buf);
    RESPOND_OK();

    return ESP_OK;
}

static esp_err_t memory_get_handler(httpd_req_t* req)
{
    esp_err_t ret;

    uint16_t address;
    ret = get_query_param_uint16(req, "address", &address);
    if(ret != ESP_OK) {
        RESPOND_ERROR_INVALID_PARAMETERS();
        return ret;
    }

    uint16_t length;
    ret = get_query_param_uint16(req, "length", &length);
    if(ret != ESP_OK) {
        RESPOND_ERROR_INVALID_PARAMETERS();
        return ret;
    }

    if(length > CHUNK_SIZE) {
        RESPOND_ERROR_INVALID_PARAMETERS();
        return ret;
    }

    ESP_LOGI(TAG, "memory read, address:0x%04x length:%i", address, length);

    char* buf = malloc(length);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Unable to reserve memory for memory put");
        RESPOND_ERROR_RECEIVING_DATA();
        return ESP_FAIL;
    }
   
    ret = fpga_comms_memory_read(address, (uint8_t*)buf, length, 0);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "Error reading from FPGA memory");
        RESPOND_ERROR_SAVING_STATE();
        free(buf);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/octet-stream"); \
    ret = httpd_resp_send(req, buf, length);

    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "Error sending response");

        free(buf);
        return ESP_FAIL;
    }

    free(buf);
    return ESP_OK;
}

esp_err_t fpga_http_endpoint_register(httpd_handle_t httpd_handle)
{
    const httpd_uri_t httpd_uri_bistream_put = {
        .uri = "/fpga/bitstream",
        .method = HTTP_PUT,
        .handler = bitstream_put_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(httpd_handle, &httpd_uri_bistream_put);

    http_api_register_json_put_endpoint(httpd_handle, "/fpga/register", register_put);
    http_api_register_json_get_endpoint(httpd_handle, "/fpga/register", register_get);

    const httpd_uri_t httpd_uri_memory_put = {
        .uri = "/fpga/memory",
        .method = HTTP_PUT,
        .handler = memory_put_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(httpd_handle, &httpd_uri_memory_put);

    const httpd_uri_t httpd_uri_memory_get = {
        .uri = "/fpga/memory",
        .method = HTTP_GET,
        .handler = memory_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(httpd_handle, &httpd_uri_memory_get);

    return ESP_OK;
}
