/*
 * app_main.c — 固件入口（骨架）
 * 阶段：仅初始化日志与占位启动；随里程碑逐步替换为真实模块装配。
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "display_task.h"
#include "protocol/config.h"
#include "render/geo.h"
#include "render/render_nav.h"
#include "comm/comm_if.h"
#include "lcd/lcd_driver.h"
#include "font/font.h"

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_LOGI(TAG, "Esp32Nav 固件骨架 v0.0.1 (IDF %s)", IDF_VER);
    ESP_LOGI(TAG, "目标: ESP32-S3 320x240 外置导航屏 | 协议 ble_protocol V1.10");

    /* 骨架占位装配：模块均提供 init_xxx() 桩，随 M1-M5 替换为真实实现 */
    config_init();
    geo_init();
    comm_if_init();
    lcd_driver_init();
    font_init();
    display_task_start();

    /* M1：直行条带渲染（内置样例，与 HTML V2 参数一致） */
    render_nav_init();
    render_nav_demo();

    ESP_LOGI(TAG, "M1: 条带渲染完成（下一步：通信承载 M2 接入帧流）");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "alive ...");
    }
}
