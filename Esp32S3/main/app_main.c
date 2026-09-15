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
#include "comm/wifi_ap.h"
#include "comm/tcp_server.h"
#include "lcd/lcd_driver.h"
#include "font/font.h"

/* 临时诊断开关：1 = 只点屏（不启动 WiFi/字库/任务），用于判定显示层；0 = 正常固件 */
#define LCD_ONLY_TEST 0   /* 点屏已验证通过（2026-09-14），正常固件流程 */

static const char *TAG = "app_main";

void app_main(void)
{
#if LCD_ONLY_TEST
    /* ==== 最小点屏测试：排除一切其它因素 ==== */
    ESP_LOGI(TAG, "LCD_ONLY_TEST: 只初始化并轮换纯色（无 WiFi/字库/任务）");
    lcd_driver_init();
    for (;;) {
        ESP_LOGI(TAG, "color cycle RED/GREEN/BLUE/WHITE/BLACK");
        lcd_probe_color_cycle();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
#endif
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

    ESP_LOGI(TAG, "M1: 条带渲染完成");

    /* M2：softAP + TCP :8899 接收协议帧（PC/手机连接后发 NAV_FRAME 即刷新画面） */
    wifi_ap_start();
    tcp_server_start();
    ESP_LOGI(TAG, "M2: 通信就绪，等待 NAV_FRAME ...");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "alive ...");
    }
}
