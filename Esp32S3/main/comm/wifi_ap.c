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

#define WIFI_TRY(expr) do {         esp_err_t err_ = (expr);         if (err_ != ESP_OK) { ESP_LOGE(TAG, "%s -> %s", #expr, esp_err_to_name(err_)); return; }     } while (0)

static bool s_common_inited = false;
static bool s_ap_started    = false;

/* 【M14】只做公共初始化：nvs / netif / 默认事件循环 / Wi-Fi 驱动。
 * 刻意**不创建 AP netif、不设模式、不 start** —— 让 wifi_sta_init() 能先以纯 STA 跑起来。 */
void wifi_common_init(void)
{
    if (s_common_inited) return;

    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs erase+reinit");
        nvs_flash_erase();
        r = nvs_flash_init();
    }
    if (r != ESP_OK) { ESP_LOGE(TAG, "nvs_flash_init -> %s", esp_err_to_name(r)); return; }

    WIFI_TRY(esp_netif_init());
    WIFI_TRY(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    WIFI_TRY(esp_wifi_init(&cfg));

    s_common_inited = true;
    ESP_LOGI(TAG, "Wi-Fi 公共初始化完成（未开热点）");
}

/* 【M14】按需启动配网热点。幂等；在 STA 已运行时切到 APSTA（set_mode 会自动重启 Wi-Fi，
 * 因此这里不再调用 esp_wifi_start()）。 */
void wifi_ap_start(void)
{
    if (!s_common_inited) wifi_common_init();
    if (s_ap_started) { ESP_LOGI(TAG, "softAP 已在运行，跳过重复启动"); return; }

    esp_netif_create_default_wifi_ap();
    WIFI_TRY(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                 wifi_ap_event, NULL, NULL));

    /* 与 STA 并存：APSTA（STA 侧若在重连，切换模式后仍会继续） */
    WIFI_TRY(esp_wifi_set_mode(WIFI_MODE_APSTA));

    wifi_config_t wc = { 0 };
    snprintf((char *)wc.ap.ssid, sizeof(wc.ap.ssid), "%s", AP_SSID);
    wc.ap.ssid_len = (uint8_t)strlen(AP_SSID);
    snprintf((char *)wc.ap.password, sizeof(wc.ap.password), "%s", AP_PASS);
    wc.ap.channel = AP_CHANNEL;
    wc.ap.max_connection = AP_MAX_STA;
    wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    WIFI_TRY(esp_wifi_set_config(WIFI_IF_AP, &wc));

    s_ap_started = true;

    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    ESP_LOGI(TAG, "softAP 已启动: SSID=%s PASS=%s channel=%d 网关=192.168.4.1",
             AP_SSID, AP_PASS, AP_CHANNEL);
    ESP_LOGI(TAG, "softAP MAC=" MACSTR " （搜不到时把完整日志发我）", MAC2STR(mac));
}
