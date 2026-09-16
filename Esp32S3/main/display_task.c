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
#define LINK_GRACE_S   5       /* TCP 无活跃连接持续超过该秒数才视为断开 */
#define FRAME_GRACE_S  30      /* 无新导航帧持续超过该秒数才视为“不能工作” */
#define BOOT_TIMEOUT_S 60      /* 兜底：60 秒仍未等到手机 App 连接也进入导航画面 */

static void display_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "display task running (开机画面>=5s -> 连接后进入导航；断线立即回开机画面)");
    int64_t last = esp_timer_get_time();
    int64_t boot_t0 = last;
    int64_t last_frame_t = last;        /* 最近一次收到导航帧的时刻 */
    int64_t last_active_t = last;       /* 最近一次“有活跃连接”的时刻 */
    uint32_t last_frames = 0;
    bool boot = true;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(33));                  /* 目标 30fps（实际受 SPI 传输限制） */
        int64_t now = esp_timer_get_time();
        float dt = (float)(now - last) / 1000000.0f;
        last = now;
        if (dt > 0.5f) dt = 0.5f;

        int64_t el = (now - boot_t0) / 1000000;         /* 距进入开机画面的秒数 */
        uint32_t fr = render_nav_frame_count();
        if (fr != last_frames) { last_frames = fr; last_frame_t = now; }
        /* 双条件（都带宽限，避免扫描/重连的短暂抖动导致闪屏）：
         * conn_ok = 5 秒内有活跃连接；frames_ok = 收到过帧且 30 秒内有新帧 */
        if (tcp_server_has_client()) last_active_t = now;
        bool conn_ok   = (now - last_active_t) < (int64_t)LINK_GRACE_S * 1000000;
        bool frames_ok = (fr > 0) && ((now - last_frame_t) < (int64_t)FRAME_GRACE_S * 1000000);
        bool active = conn_ok && frames_ok;

        if (boot) {                                     /* 开机画面：能工作且满 5 秒才切；或超时兜底 */
            if ((active && el >= BOOT_MIN_S) || el > BOOT_TIMEOUT_S) {
                boot = false;
                if (fr == 0) render_nav_demo();         /* 兜底切换且无数据：显示样例画面，避免黑屏 */
                ESP_LOGI(TAG, "boot -> nav (frames=%lu, elapsed=%llds)",
                         (unsigned long)fr, (long long)el);
            } else {
                char sub[32];
                int stage = (el < 1) ? 0 : (wifi_sta_is_connected() ? 2 : 1);
                if (stage == 2)      snprintf(sub, sizeof(sub), "IP %s", wifi_sta_ip_str());
                else if (stage == 1) snprintf(sub, sizeof(sub), "热点 ESPNav-AP");
                else                 sub[0] = 0;
                render_nav_boot(stage, sub);
                continue;
            }
        } else if (!active) {                           /* 判定“不能工作”（已含宽限）才回开机画面 */
            boot = true;
            boot_t0 = now;
            ESP_LOGW(TAG, "not workable (conn_ok=%d frames_ok=%d) -> back to boot screen",
                     (int)conn_ok, (int)frames_ok);
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
