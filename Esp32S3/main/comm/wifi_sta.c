#include "wifi_sta.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "wifi_sta";

#define NVS_NS   "espnav_wifi"
#define KEY_CNT  "count"
#define RETRY_MS 10000

static bool          s_inited = false;
static volatile bool s_connected = false;
static char          s_ip[16] = "";

/* ---------------- NVS 凭据 ---------------- */

static bool cred_read(int i, char *ssid, size_t ssid_sz, char *pass, size_t pass_sz)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    char k[12];
    size_t l = ssid_sz;
    snprintf(k, sizeof(k), "ssid%d", i);
    esp_err_t r = nvs_get_str(h, k, ssid, &l);
    if (r == ESP_OK) {
        snprintf(k, sizeof(k), "pass%d", i);
        l = pass_sz;
        if (nvs_get_str(h, k, pass, &l) != ESP_OK) pass[0] = 0;
    }
    nvs_close(h);
    return r == ESP_OK && ssid[0] != 0;
}

int wifi_sta_cred_count(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return 0;
    uint8_t n = 0;
    nvs_get_u8(h, KEY_CNT, &n);
    nvs_close(h);
    if (n > WIFI_STA_MAX_CRED) n = WIFI_STA_MAX_CRED;
    return (int)n;
}

bool wifi_sta_has_saved(void) { return wifi_sta_cred_count() > 0; }

bool wifi_sta_save_credential(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) return false;

    const int n = wifi_sta_cred_count();
    int slot = -1;
    char es[64], ep[64];
    for (int i = 0; i < n; i++) {
        if (cred_read(i, es, sizeof(es), ep, sizeof(ep)) && strcmp(es, ssid) == 0) { slot = i; break; }
    }
    if (slot < 0) slot = (n >= WIFI_STA_MAX_CRED) ? 0 : n;   /* 满则覆盖最旧 */

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open 失败，凭据未保存");
        return false;
    }
    char k[12];
    snprintf(k, sizeof(k), "ssid%d", slot);
    nvs_set_str(h, k, ssid);
    snprintf(k, sizeof(k), "pass%d", slot);
    nvs_set_str(h, k, pass ? pass : "");
    if (slot == n && n < WIFI_STA_MAX_CRED) nvs_set_u8(h, KEY_CNT, (uint8_t)(n + 1));
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "凭据已保存: slot=%d ssid=%s", slot, ssid);
    return true;
}

void wifi_sta_erase_all(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
    s_connected = false;
    s_ip[0] = 0;
    ESP_LOGW(TAG, "已清除全部 Wi-Fi 凭据");
}

/* ---------------- 事件 ---------------- */

static void sta_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_ip[0] = 0;
        ESP_LOGW(TAG, "STA 断开（由重连任务轮换凭据重试）");
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        s_connected = true;
        ESP_LOGI(TAG, "STA 连接成功 IP=%s（手机/PC 可用该 IP，或 http://espnav.local:8899）", s_ip);
    }
}

/* ---------------- 连接/重连任务 ---------------- */

static void sta_task(void *arg)
{
    (void)arg;
    char ssid[64], pass[64];
    int idx = 0;

    for (;;) {
        if (!s_connected) {
            const int n = wifi_sta_cred_count();
            if (n > 0) {
                if (idx >= n) idx = 0;
                if (cred_read(idx, ssid, sizeof(ssid), pass, sizeof(pass))) {
                    wifi_config_t wc = { 0 };
                    snprintf((char *)wc.sta.ssid, sizeof(wc.sta.ssid), "%s", ssid);
                    snprintf((char *)wc.sta.password, sizeof(wc.sta.password), "%s", pass);
                    wc.sta.threshold.authmode = WIFI_AUTH_OPEN;
                    esp_wifi_set_config(WIFI_IF_STA, &wc);
                    ESP_LOGI(TAG, "尝试连接 [%d/%d] SSID=%s", idx + 1, n, ssid);
                    esp_wifi_connect();
                }
                idx++;                       /* 本轮失败 -> 换下一组凭据 */
            }
        }
        vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
    }
}

/* ---------------- 对外接口 ---------------- */

void wifi_sta_init(void)
{
    if (s_inited) return;

    esp_netif_create_default_wifi_sta();     /* AP 的 netif 已在 wifi_ap_start() 创建 */
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, sta_event, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, sta_event, NULL, NULL);

    /* AP + STA 并存；set_mode 会自动重启 Wi-Fi，无需再 esp_wifi_start() */
    esp_err_t r = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (r != ESP_OK) ESP_LOGE(TAG, "set_mode(APSTA) -> %s", esp_err_to_name(r));

    s_inited = true;
    xTaskCreate(sta_task, "wifi_sta", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "STA 就绪，保存凭据 %d 组%s", wifi_sta_cred_count(),
             wifi_sta_cred_count() ? "（将自动连接）" : "（请用配网页 http://192.168.4.1 添加）");
}

bool wifi_sta_is_connected(void) { return s_connected; }

const char *wifi_sta_ip_str(void) { return s_ip; }

int wifi_sta_rssi(void)
{
    wifi_ap_record_t ap;
    if (!s_connected) return 0;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return 0;
    return ap.rssi;
}
