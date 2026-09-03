#include "display_task.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "display_task";

static void display_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "display task running (30fps 渲染待 M1/M3 接入)");
    for (;;) {
        /* TODO(M1): 按 dash_speed 推进虚线 offset 并重绘变化区域/整帧 */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void display_task_start(void)
{
    ESP_LOGI(TAG, "starting ...");
    xTaskCreate(display_task, "nav_display", 4096, NULL, 5, NULL);
}
