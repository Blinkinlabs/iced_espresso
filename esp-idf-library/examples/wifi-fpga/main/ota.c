#include "ota.h"
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <string.h>

static const char TAG[] = "ota";

static esp_partition_t* partition = NULL;
static esp_ota_handle_t ota_handle;

esp_err_t ota_start(const size_t image_length)
{
    // Find the next OTA partition and initialize it for OTA update
    partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL) {
        ESP_LOGE(TAG, "Error getting next partition");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_ota_begin(partition, image_length, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error starting OTA");
    }

    return ESP_OK;
}

//! @brief Add data to an in-progress OTA update
esp_err_t ota_add_chunk(const char* chunk, const int length)
{
    return esp_ota_write(ota_handle, chunk, length);
}

//! @brief Finish an OTA operation
esp_err_t ota_finalize()
{

    esp_err_t ret = esp_ota_end(ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error finishing OTA");
    }

    ret = esp_ota_set_boot_partition(partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error updating boot partition");
    }

    return ret;
}

esp_err_t ota_abort()
{
    return esp_ota_abort(ota_handle);
}

esp_err_t ota_init()
{
    const esp_partition_t* running_partition = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Booted from partition %s at address %i",
        running_partition->label, running_partition->address);

    esp_ota_img_states_t esp_ota_img_state;
    const esp_err_t ret = esp_ota_get_state_partition(running_partition, &esp_ota_img_state);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error getting OTA partition state");
        return ret;
    }

    if (esp_ota_img_state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "Marking new partition as valid");
        esp_ota_mark_app_valid_cancel_rollback();
    }

    return ESP_OK;
}
