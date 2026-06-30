#include "user_ota_update.h"



static const char *OTA_TAG = "OTA";


// Fetch the available version string from your REST endpoint
esp_err_t get_available_version(char *version_buf, size_t max_len) 
{
    esp_http_client_config_t config = 
    {
        .url = VERSION_CHECK_URL,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    bool success = false;

    esp_err_t open_ret = esp_http_client_open(client, 0);
    if (open_ret != ESP_OK)
    {
        return open_ret;
    }

    int64_t content_length = esp_http_client_fetch_headers(client);
    if (content_length > 0) 
    {
        int read_bytes = esp_http_client_read_response(client, version_buf, max_len - 1);
        if (read_bytes > 0) 
        {
            version_buf[read_bytes] = '\0';
            // Clean trailing newlines if any
            version_buf[strcspn(version_buf, "\r\n")] = '\0';
            success = true;
        }
    }
    
    esp_err_t clean_ret = esp_http_client_cleanup(client);
    return clean_ret;
}

esp_err_t execute_https_ota_update(void)
{
    esp_http_client_config_t http_config = 
    {
        .url = FIRMWARE_DOWNLOAD_URL,
        // If your server uses HTTPS, add .cert_pem here to prevent MITM attacks
    };
    esp_https_ota_config_t ota_config = 
    {
        .http_config = &http_config,
    };

    esp_err_t ota_ret = esp_https_ota(&ota_config);
    if (ota_ret == ESP_OK) 
    {
        ESP_LOGI(OTA_TAG, "OTA update completed");
        // 10. Reboot device
        ESP_LOGI(OTA_TAG, "Restarting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
        return ESP_OK;
    } else 
    {
        // Ideal behavior: Handle power cut/failure safely via rollback, show red indication
        ESP_LOGE(OTA_TAG, "OTA Update Failed! Error: %s", esp_err_to_name(ota_ret));
        return ota_ret;
    }
    return ESP_OK;
}

 