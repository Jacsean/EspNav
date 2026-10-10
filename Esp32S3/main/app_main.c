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
#include "board/battery.h"   /* 【M7】板载电池电压检测（BAT_ADC = GPIO9） */
#include "audio/audio_out.h"  /* 【M13】音频输出（I2S → ES8311 → 功放） */

/* 临时诊断开关：1 = 只点屏（不启动 WiFi/字库/任务），用于判定显示层；0 = 正常固件 */
#define LCD_ONLY_TEST 0   /* 点屏已验证通过（2026-09-14），正常固件流程 */

static const char *TAG = "app_main";

/* 【M14/M15】开机"先尝试连接已保存热点"的等待时长，现在由配置项 ap_delay_s 决定
 * （默认 15 秒；App 可修改并持久化到 NVS，下次上电生效）。 */

/* 【M7】电量采样任务：2s 采一次（9 样本取中位数在 battery_poll 内完成）。
 * 只写 battery 模块内部缓存，渲染任务只读 —— 不碰帧缓冲。 */
static void battery_task(void *arg)
{
    (void)arg;
    for (;;) {
        battery_poll();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

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

    /* 【M7】电量：初始化 ADC（BAT_ADC=GPIO9）并起 2s 采样任务 */
    battery_init();
    xTaskCreate(battery_task, "battery", 2560, NULL, 3, NULL);

    /* 【M13 第1批】音频输出：初始化 I2S + ES8311 + 功放，并播 3 声测试音自检。
     * 听到连续三声"嘀"即说明音频链路（I2C 配置 → I2S → ES8311 → 功放 → 喇叭）是通的。 */
    audio_out_init();
    audio_out_test_tone();

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
    /* 【M14】联网顺序（用户要求）：
     *   1) 先以**纯 STA** 启动，尝试连接 NVS 里已保存的热点（手机热点/路由器）
     *   2) 最多等 STA_WAIT_S 秒；连上就直接对外服务（**不产生热点**）
     *   3) 仍连不上（或压根没有凭据）→ 才启动配网热点 ESPNav-AP（配网页 192.168.4.1）
     *   4) 运行中若 STA 掉线且热点还没开 → 补开，否则再也进不了配网页
     * 这样"通电就冒出一个热点"的行为就消除了。 */
    wifi_common_init();
    config_load_nvs();                  /* 【M15】载入 NVS 里的 ap_delay_s（须在 NVS init 之后）*/
    wifi_sta_init();

    /* 【M15】等待时长来自配置（App 可设，存 NVS，下次上电生效） */
    const int wait_s = (int)config_get()->ap_delay_s;
    bool sta_ok = false;
    if (wifi_sta_has_saved()) {
        ESP_LOGI(TAG, "NVS 有 WiFi 凭据：尝试连接，最多等 %d 秒（可由 App 设置）…", wait_s);
        for (int i = 0; i < wait_s * 5; i++) {              /* 每 200ms 查一次 */
            if (wifi_sta_is_connected()) { sta_ok = true; break; }
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    } else {
        ESP_LOGI(TAG, "NVS 无 WiFi 凭据 → 直接进入配网模式");
    }

    static bool s_ap_on = false;
    static bool s_prov_on = false;
    if (sta_ok) {
        ESP_LOGI(TAG, "已连上热点：IP=%s（不启动配网热点）", wifi_sta_ip_str());
    } else {
        ESP_LOGW(TAG, "未连上热点 → 启动配网热点 ESPNav-AP（配网页 http://192.168.4.1）");
        wifi_ap_start();
        softap_prov_init();
        s_ap_on = true;
        s_prov_on = true;
    }
    mdns_service_init();
    tcp_server_start();
    ESP_LOGI(TAG, "M14: 通信就绪（STA=%s IP=%s / 配网页 %s / TCP:8899）",
             wifi_sta_is_connected() ? "已连接" : "未连接",
             wifi_sta_ip_str()[0] ? wifi_sta_ip_str() : "-",
             s_ap_on ? "http://192.168.4.1" : "未开启");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        /* 【M14】保险：运行中 STA 掉线而热点尚未开启 → 补开配网热点
         * （否则"连上热点后 AP 从未启动"，一旦掉线就再也连不上、也进不了配网页） */
        if (!s_ap_on && !wifi_sta_is_connected()) {
            ESP_LOGW(TAG, "检测到 STA 掉线且未开热点 → 补开配网热点 ESPNav-AP");
            wifi_ap_start();
            if (!s_prov_on) { softap_prov_init(); s_prov_on = true; }
            s_ap_on = true;
        }
        ESP_LOGI(TAG, "alive ... STA=%s ip=%s rssi=%d AP_clients_ok",
                 wifi_sta_is_connected() ? "已连接" : "未连接",
                 wifi_sta_ip_str()[0] ? wifi_sta_ip_str() : "-", wifi_sta_rssi());
    }
}
