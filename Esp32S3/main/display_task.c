#include "display_task.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "render/render_nav.h"
#include "comm/wifi_sta.h"
#include "comm/tcp_server.h"
#include "protocol/protocol.h"

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
    int64_t nav_t0 = 0;                  /* 首次进入“导航中”的时刻（用于最短 5 秒） */
    bool nav_on = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(33));                  /* 目标 30fps（实际受 SPI 传输限制） */
        int64_t now = esp_timer_get_time();
        float dt = (float)(now - last) / 1000000.0f;
        last = now;
        if (dt > 0.5f) dt = 0.5f;

        /* 阶段判定唯一依据：最近收到什么报文（见 protocol_link_stage） */
        int stage = protocol_link_stage();          /* 1 等待App / 2 已连等待导航 / 3 导航中 */

        if (stage < 3) {                            /* 非导航阶段：显示开机画面 */
            nav_on = false;
            nav_t0 = 0;
            char sub[32];
            if (stage == 1) {
                if (wifi_sta_is_connected()) snprintf(sub, sizeof(sub), "IP %s", wifi_sta_ip_str());
                else                         snprintf(sub, sizeof(sub), "热点 ESPNav-AP");
            } else {
                sub[0] = 0;                         /* 阶段 2：主行已说明，无副行 */
            }
            render_nav_boot((stage == 1) ? 2 : 3, sub);   /* 画面: 2=等待手机App连接 3=已连接-等待导航数据 */
            continue;
        }
        /* 阶段 3：导航中 —— 先保证开机画面显示满最短时间，再切导航画面 */
        if (!nav_on) {
            if (nav_t0 == 0) nav_t0 = now;
            if ((now - nav_t0) < (int64_t)BOOT_MIN_S * 1000000) {
                render_nav_boot(3, "");
                continue;
            }
            nav_on = true;
            ESP_LOGI(TAG, "boot -> nav (link stage=3, 已显示满 %ds)", BOOT_MIN_S);
        }
        render_nav_tick(dt);
    }
}

void display_task_start(void)
{
    ESP_LOGI(TAG, "starting ...");
    xTaskCreate(display_task, "nav_display", 12288, NULL, 5, NULL);   /* 8KB：字体/文字渲染需要 */
}
