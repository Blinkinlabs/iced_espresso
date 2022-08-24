#include "ota_http_endpoint.h"
#include "ota.h"
#include "http_response.h"
#include <esp_log.h>

static const char* TAG = "ota_http_endpoint";

#define CHUNK_SIZE (2048)

static esp_err_t ota_put_handler(httpd_req_t* req)
{
    esp_err_t ret;

    char* buf = malloc(CHUNK_SIZE);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Unable to reserve memory for OTA, try rebooting");
        RESPOND_ERROR_RECEIVING_DATA();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Starting OTA, length:%i", req->content_len);
    ret = ota_start(req->content_len);

    size_t remaining = req->content_len;

    while (remaining > 0) {
        const size_t chunk_size = ((CHUNK_SIZE < remaining) ? CHUNK_SIZE : remaining);

        /* Read the data for the request */
        const int received = httpd_req_recv(req, buf, chunk_size);
        ESP_LOGI(TAG, "OTA, chunk_size:%i remaining:%i", chunk_size, remaining);

        if (received <= 0) {
            ESP_LOGE(TAG, "OTA error receiving data, received:%i", received);
            ota_abort();
            RESPOND_ERROR_RECEIVING_DATA();
            free(buf);
            return ESP_FAIL;
        }

        if (received != chunk_size) {
            ESP_LOGE(TAG, "OTA too little data received, received:%i", received);
        }

        ret = ota_add_chunk(buf, received);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA error adding chunk, ret:%i", ret);
            ota_abort();
            RESPOND_ERROR_RECEIVING_DATA();
            free(buf);
            return ESP_FAIL;
        }

        remaining -= received;
    }

    free(buf);

    ESP_LOGI(TAG, "Finishing OTA");
    ret = ota_finalize();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error performing OTA update");
        RESPOND_ERROR_APPLYING_STATE();
        return ret;
    }

    ESP_LOGI(TAG, "OTA update successful, restarting");
    RESPOND_OK();

    // TODO: Check that response was sent, then reboot
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    esp_restart();

    return ESP_OK;
}

esp_err_t ota_http_endpoint_register(httpd_handle_t handle, const char* uri) {
    if (handle == NULL) {
        return ESP_FAIL;
    }

    const httpd_uri_t httpd_uri = {
        .uri = uri,
        .method = HTTP_PUT,
        .handler = ota_put_handler,
        .user_ctx = NULL
    };

    return httpd_register_uri_handler(handle, &httpd_uri);
}
