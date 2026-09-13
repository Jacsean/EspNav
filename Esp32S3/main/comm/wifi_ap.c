#include "wifi_ap.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_mac.h"
#include "esp_log.h"

static const char *TAG = "wifi_ap";

#define AP_SSID     "ESPNav-AP"
#define AP_PASS     "espnav1234"
#define AP_CHANNEL  1
#define AP_MAX_STA  2

static void wifi_ap_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base != WIFI_EVENT) return;
    if (id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "AP_START: 热点就绪 -> SSID=%s  密码=%s  网关=192.168.4.1", AP_SSID, AP_PASS);
    } else if (id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "客户端已连接: " MACSTR, MAC2STR(e->mac));
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "客户端已断开");
    }
}

void wifi_ap_start(void)
{
#define WIFI_TRY(expr) do {         esp_err_t err_ = (expr);         if (err_ != ESP_OK) { ESP_LOGE(TAG, "%s -> %s", #expr, esp_err_to_name(err_)); return; }     } while (0)

    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs erase+reinit");
        nvs_flash_erase();
        r = nvs_flash_init();
    }
    if (r != ESP_OK) { ESP_LOGE(TAG, "nvs_flash_init -> %s", esp_err_to_name(r)); return; }

    WIFI_TRY(esp_netif_init());
    WIFI_TRY(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    WIFI_TRY(esp_wifi_init(&cfg));

    wifi_config_t wc = { 0 };
    snprintf((char *)wc.ap.ssid, sizeof(wc.ap.ssid), "%s", AP_SSID);
    wc.ap.ssid_len = (uint8_t)strlen(AP_SSID);
    snprintf((char *)wc.ap.password, sizeof(wc.ap.password), "%s", AP_PASS);
    wc.ap.channel = AP_CHANNEL;
    wc.ap.max_connection = AP_MAX_STA;
    wc.ap.authmode = WIFI_AUTH_WPA2_PSK;

    WIFI_TRY(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_ap_event, NULL, NULL));
    WIFI_TRY(esp_wifi_set_mode(WIFI_MODE_AP));
    WIFI_TRY(esp_wifi_set_config(WIFI_IF_AP, &wc));
    WIFI_TRY(esp_wifi_start());

    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    ESP_LOGI(TAG, "softAP 启动请求已发出: SSID=%s PASS=%s channel=%d", AP_SSID, AP_PASS, AP_CHANNEL);
    ESP_LOGI(TAG, "softAP MAC=" MACSTR " （若 PC/手机搜不到此热点，请把本次完整日志发我）", MAC2STR(mac));
}
