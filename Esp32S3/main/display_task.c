#include "display_task.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "render/render_nav.h"
#include "comm/wifi_sta.h"
#include "comm/tcp_server.h"

static const char *TAG = "display_task";

#define BOOT_MIN_S     5       /* 开机画面最短显示时间（画面切换延迟；后台照常收数据） */
#define BOOT_TIMEOUT_S 60      /* 兜底：60 秒仍未等到手机 App 连接也进入导航画面 */

static void display_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "display task running (开机画面>=5s -> 连接后进入导航；断线立即回开机画面)");
    int64_t last = esp_timer_get_time();
    int64_t boot_t0 = last;
    bool boot = true;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(33));                  /* 目标 30fps（实际受 SPI 传输限制） */
        int64_t now = esp_timer_get_time();
        float dt = (float)(now - last) / 1000000.0f;
        last = now;
        if (dt > 0.5f) dt = 0.5f;

        int64_t el = (now - boot_t0) / 1000000;         /* 距进入开机画面的秒数 */
        bool linked = tcp_server_has_client();

        if (boot) {                                     /* 开机画面：已连且满 5 秒才切；或超时兜底 */
            if ((linked && el >= BOOT_MIN_S) || el > BOOT_TIMEOUT_S) {
                boot = false;
                ESP_LOGI(TAG, "boot -> nav (linked=%d, elapsed=%llds)", (int)linked, (long long)el);
            } else {
                char sub[32];
                int stage = (el < 1) ? 0 : (wifi_sta_is_connected() ? 2 : 1);
                if (stage == 2)      snprintf(sub, sizeof(sub), "IP %s", wifi_sta_ip_str());
                else if (stage == 1) snprintf(sub, sizeof(sub), "热点 ESPNav-AP");
                else                 sub[0] = 0;
                render_nav_boot(stage, sub);
                continue;
            }
        } else if (!linked) {                           /* 断线：立即回开机画面（不显示“信号中断”） */
            boot = true;
            boot_t0 = now;
            ESP_LOGW(TAG, "app disconnected -> back to boot screen");
            continue;
        }
        render_nav_tick(dt);
        static uint32_t n = 0;
        n++;
        if (n == 1 || (n % 150) == 0) {
            ESP_LOGI(TAG, "render tick #%lu (dt=%.3fs)", (unsigned long)n, dt);
        }
    }
}

void display_task_start(void)
{
    ESP_LOGI(TAG, "starting ...");
    xTaskCreate(display_task, "nav_display", 12288, NULL, 5, NULL);   /* 8KB：字体/文字渲染需要 */
}
