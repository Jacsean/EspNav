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

#define LINK_GRACE_S   5       /* TCP 无活跃连接持续超过该秒数才视为断开 */
#define FRAME_GRACE_S  30      /* 无新导航帧持续超过该秒数才视为“不能工作” */

static void display_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "display task running (纯报文驱动：有报文=导航画面 / 无报文=开机画面)");
    int64_t last = esp_timer_get_time();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(33));                  /* 目标 30fps（实际受 SPI 传输限制） */
        int64_t now = esp_timer_get_time();
        float dt = (float)(now - last) / 1000000.0f;
        last = now;
        if (dt > 0.5f) dt = 0.5f;

        /* 唯一判据：5 秒内有没有收到报文（具名常量，禁止魔法数字比较） */
        if (protocol_link_stage() == LINK_STAGE_WAIT_APP) {
            char sub[32];
            if (wifi_sta_is_connected()) snprintf(sub, sizeof(sub), "IP %s", wifi_sta_ip_str());
            else                         snprintf(sub, sizeof(sub), "热点 ESPNav-AP");
            render_nav_boot(2, sub);                    /* 主行：等待手机App连接 */
            continue;
        }
        /* 已连接：画面内容完全由报文决定
         *   收到 NAV_FRAME   -> 实时导航内容
         *   只收到 PING/HELLO -> 导航版式 + "请在App开始导航"
         *   收到 CLEAR_SCREEN -> 黑屏保持                                  */
        render_nav_tick(dt);
    }
}

void display_task_start(void)
{
    ESP_LOGI(TAG, "starting ...");
    xTaskCreate(display_task, "nav_display", 12288, NULL, 5, NULL);   /* 8KB：字体/文字渲染需要 */
}
