#include "wifi_ap.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "wifi_ap";
#define AP_SSID     "ESPNav-AP"
#define AP_PASS     "espnav1234"
#define AP_CHANNEL  1
#define AP_MAX_STA  2

void wifi_ap_start(void)
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        r = nvs_flash_init();
    }
    ESP_ERROR_CHECK(r);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.ap.ssid, AP_SSID, sizeof(wc.ap.ssid) - 1);
    wc.ap.ssid_len = (uint8_t)strlen(AP_SSID);
    strncpy((char *)wc.ap.password, AP_PASS, sizeof(wc.ap.password) - 1);
    wc.ap.channel = AP_CHANNEL;
    wc.ap.max_connection = AP_MAX_STA;
    wc.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "softAP 已启动: SSID=%s PASS=%s -> TCP 目标 192.168.4.1:8899", AP_SSID, AP_PASS);
}
