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
#include "comm/wifi_sta.h"
#include "comm/softap_prov.h"
#include "comm/mdns_service.h"
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

    /* M6：承载启动顺序
     * 1) softAP（兼容：手机可直连 ESPNav-AP 调试/配网）
     * 2) STA：用 NVS 保存的凭据连手机热点/路由器（使手机保持外网；APSTA 并存）
     * 3) 配网页：http://192.168.4.1 搜索/填写 WiFi，保存后自动连接
     * 4) mDNS：espnav.local（需 espressif/mdns 组件，当前为空实现）
     * 5) TCP :8899：接收 NAV_FRAME */
    wifi_ap_start();
    wifi_sta_init();
    softap_prov_init();
    mdns_service_init();
    tcp_server_start();
    ESP_LOGI(TAG, "M6: 通信就绪（AP:192.168.4.1 / 配网页:http://192.168.4.1 / TCP:8899）");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "alive ... STA=%s ip=%s rssi=%d AP_clients_ok",
                 wifi_sta_is_connected() ? "已连接" : "未连接",
                 wifi_sta_ip_str()[0] ? wifi_sta_ip_str() : "-", wifi_sta_rssi());
    }
}
