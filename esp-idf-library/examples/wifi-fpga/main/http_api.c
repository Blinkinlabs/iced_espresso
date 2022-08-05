#include "http_api.h"
#include "fpga_loader.h"
#include "http_response.h"
#include "ota.h"
#include <esp_event.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <math.h>

static const char* TAG = "http_server";

static httpd_handle_t server = NULL;

#define OTA_CHUNK_SIZE (2048)

#if 0
//! @brief Decode a URLencoded string
//!
//! TODO: Nice and untested
//!
//! @param[in] intput String to decode
//! @param[out] output Buffer to place decoded string
//! @param[in] output_size Size of output buffer, in bytes
static void urlDecode(const char* input, char* output, int output_size)
{
    memset(output, 0, output_size);

    unsigned int in_pos = 0;
    unsigned int out_pos = 0;

    while (in_pos < strlen(input)) {
        switch (input[in_pos]) {
        case '%':
            if (!isxdigit((unsigned char)input[in_pos + 1]) || !isxdigit((unsigned char)input[in_pos + 2])) {
                output[out_pos++] = input[in_pos];
                in_pos += 1;
            } else {
                output[out_pos++] = ((input[in_pos + 1] - '0') << 4) | (input[in_pos + 2] - '0');
                in_pos += 3;
            }
            break;
        case '+':
            output[out_pos++] = ' ';
            in_pos += 1;
            break;
        default:
            output[out_pos++] = input[in_pos];
            in_pos += 1;
        }

        if (out_pos >= output_size)
            break;
    }
}

bool get_string_id_from_uri(const char* uri, const char* header, char* id, int id_size)
{
    int starting_pos = strlen(header);

    if (strlen(uri) <= starting_pos)
        return false;

    //strlcpy(id, &uri[starting_pos], id_size);
    urlDecode(uri + starting_pos, id, id_size);

    return true;
}

#define ID_MAX_LEN 10

bool get_int_id_from_uri(const char* uri, const char* header, int* id)
{
    char string_id[ID_MAX_LEN];

    if (!get_string_id_from_uri(uri, header, string_id, sizeof(string_id)))
        return false;

    // TODO: Test if the whole string was valid, currently errors evaluate to 0
    *id = atoi(string_id);
    return true;
}
#endif

static esp_err_t ota_put_handler(httpd_req_t* req)
{
    esp_err_t ret;

    char* buf = malloc(OTA_CHUNK_SIZE);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Unable to reserve memory for OTA, try rebooting");
        RESPOND_ERROR_RECEIVING_DATA();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Starting OTA, length:%i", req->content_len);
    ret = ota_start(req->content_len);
    // TODO: Handle ret

    size_t remaining = req->content_len;

    while (remaining > 0) {
        const size_t chunk_size = ((OTA_CHUNK_SIZE < remaining) ? OTA_CHUNK_SIZE : remaining);

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

        if (received < chunk_size) {
            ESP_LOGD(TAG, "OTA received less data than requested, received:%i requested:%i",
                    received,
                    chunk_size);
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

// TODO link to fpga_loader.c
#define CONFIG_FPGA_LOADER_SIZE (CONFIG_FPGA_SPI_BUFFER_SIZE * 4)

static esp_err_t fpga_put_handler(httpd_req_t* req)
{
    esp_err_t ret;

    char* buf = malloc(CONFIG_FPGA_LOADER_SIZE);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Unable to reserve memory for FPGA upload, try rebooting");
        RESPOND_ERROR_RECEIVING_DATA();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Starting FPGA upload, length:%i", req->content_len);
    ret = fpga_loader_start();
    // TODO: Handle ret

    size_t remaining = req->content_len;

    while (remaining > 0) {
        const size_t chunk_size = ((CONFIG_FPGA_LOADER_SIZE < remaining) ? CONFIG_FPGA_LOADER_SIZE : remaining);

        /* Read the data for the request */
        const int received = httpd_req_recv(req, buf, chunk_size);
        ESP_LOGI(TAG, "FPGA, chunk_size:%i remaining:%i", chunk_size, remaining);

        if (received <= 0) {
            ESP_LOGE(TAG, "FPGA error receiving data, received:%i", received);
            fpga_loader_abort();
            RESPOND_ERROR_RECEIVING_DATA();
            free(buf);
            return ESP_FAIL;
        }

        if (received < chunk_size) {
            ESP_LOGD(TAG, "FPGA received less data than requested, received:%i requested:%i",
                    received,
                    chunk_size);
        }

        ret = fpga_loader_add_chunk(buf, received);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "FPGA error adding chunk, ret:%i", ret);
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
        ESP_LOGE(TAG, "Error performing FPGA update");
        RESPOND_ERROR_APPLYING_STATE();
        return ret;
    }

    ESP_LOGI(TAG, "FPGA update successful, restarting");
    RESPOND_OK();

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
    cJSON* json = cJSON_ParseWithLength(buf, ret);
    if (json == NULL) {
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
    const esp_err_t err = ((http_api_json_put_callback_t)req->user_ctx)(json);

    cJSON_Delete(json);

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
    cJSON* json = cJSON_ParseWithLength(buf, ret);

    if (json == NULL) {
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
    const esp_err_t err = ((http_api_json_get_callback_t)req->user_ctx)(json, &response);

    cJSON_Delete(json);

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

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_uri_handlers = 20;
	config.uri_match_fn = httpd_uri_match_wildcard;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // TODO: do endpoints need to be registerd at every start?
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

static void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    httpd_handle_t* httpd_handle = (httpd_handle_t*)arg;
    if (*httpd_handle) {
        ESP_LOGI(TAG, "Stopping webserver");
        stop_webserver(*httpd_handle);
        *httpd_handle = NULL;
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    httpd_handle_t* httpd_handle = (httpd_handle_t*)arg;
    if (*httpd_handle == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *httpd_handle = start_webserver();
    }
}

void http_api_init()
{

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));

    /* Start the server for the first time */
    {
        server = start_webserver();

        const httpd_uri_t httpd_uri = {
            .uri = "/ota",
            .method = HTTP_PUT,
            .handler = ota_put_handler,
            .user_ctx = NULL
        };

        // TODO: Do these persist over a stop/start call?
        httpd_register_uri_handler(server, &httpd_uri);
    }
    {
        const httpd_uri_t httpd_uri = {
            .uri = "/fpga/bitstream",
            .method = HTTP_PUT,
            .handler = fpga_put_handler,
            .user_ctx = NULL
        };

        // TODO: Do these persist over a stop/start call?
        httpd_register_uri_handler(server, &httpd_uri);
    }
}

esp_err_t http_api_register_json_put_endpoint(const char* uri,
    http_api_json_put_callback_t callback)
{
    if (server == NULL) {
        return ESP_FAIL;
    }

    const httpd_uri_t httpd_uri = {
        .uri = uri,
        .method = HTTP_PUT,
        .handler = json_put_handler,
        .user_ctx = callback
    };

    // TODO: Do these persist over a stop/start call?
    return httpd_register_uri_handler(server, &httpd_uri);
}

esp_err_t http_api_register_json_get_endpoint(const char* uri,
    http_api_json_get_callback_t callback)
{
    if (server == NULL) {
        return ESP_FAIL;
    }

    const httpd_uri_t httpd_uri = {
        .uri = uri,
        .method = HTTP_GET,
        .handler = json_get_handler,
        .user_ctx = callback
    };

    // TODO: Do these persist over a stop/start call?
    return httpd_register_uri_handler(server, &httpd_uri);
}

esp_err_t http_api_register_binary_put_endpoint(const char* uri,
    http_api_binary_put_callback_t callback)
{
    if (server == NULL) {
        return ESP_FAIL;
    }

    const httpd_uri_t httpd_uri = {
        .uri = uri,
        .method = HTTP_PUT,
        .handler = binary_put_handler,
        .user_ctx = callback
    };

    // TODO: Do these persist over a stop/start call?
    return httpd_register_uri_handler(server, &httpd_uri);
}
