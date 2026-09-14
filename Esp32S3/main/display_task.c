#include "display_task.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "render/render_nav.h"

static const char *TAG = "display_task";

static void display_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "display task running (周期渲染 + 虚线流动动画)");
    int64_t last = esp_timer_get_time();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(33));                  /* 目标 30fps（实际受 SPI 传输限制） */
        int64_t now = esp_timer_get_time();
        float dt = (float)(now - last) / 1000000.0f;
        last = now;
        if (dt > 0.5f) dt = 0.5f;
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
    xTaskCreate(display_task, "nav_display", 8192, NULL, 5, NULL);   /* 8KB：字体/文字渲染需要 */
}
