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

/* 【M16.1】音频启动任务：延迟后初始化音频并播自检音；失败只记日志，不影响整机。
 * 为什么独立成任务：音频是可选功能，显示与联网必须先跑起来。 */
static void audio_boot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(3000));            /* 先让显示与联网起来 */
    if (audio_out_init() != ESP_OK) {
        ESP_LOGE(TAG, "音频初始化失败 —— 已跳过（导航/联网不受影响）");
        vTaskDelete(NULL);
        return;
    }
    audio_out_test_tone();                      /* 自检音：喇叭接好后能听到 3 声 */
    /* 【M22】应用 NVS 里保存的语音音量（默认 200%）。
     * 放在这里是因为配置已由 app_main 调 config_load_nvs() 载入。 */
    audio_out_set_volume((int)config_get()->audio_vol);
    vTaskDelete(NULL);
}

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

    /* 【M16.1】音频初始化**不在主流程里做** —— 它涉及 I2S/I2C 两块新硬件，
     * 一旦失败绝不能拖垮显示与联网（此前直接调用且内部用 ESP_ERROR_CHECK，
     * 失败即 abort → 重启循环 → 屏幕全白 + 热点起不来）。
     * 改为**独立任务**：延迟 3 秒等显示/联网就绪后再初始化，失败只记日志。 */
    xTaskCreate(audio_boot_task, "audio_boot", 4096, NULL, 3, NULL);

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

    /* 【M16.2】先把"有没有凭据"打出来 —— 这是"为什么上电就出热点"的唯一判据。
     * 注意：手机热点**有密码**，ESP 不可能凭空连上；必须先配网一次把凭据存进 NVS。 */
    ESP_LOGI(TAG, "NVS 里的 WiFi 凭据数 = %d", wifi_sta_cred_count());

    bool sta_ok = false;
    if (wifi_sta_has_saved()) {
        ESP_LOGI(TAG, "NVS 有 WiFi 凭据：尝试连接，最多等 %d 秒（可由 App 设置）…", wait_s);
        for (int i = 0; i < wait_s * 5; i++) {              /* 每 200ms 查一次 */
            if (wifi_sta_is_connected()) { sta_ok = true; break; }
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    } else {
        ESP_LOGW(TAG, "NVS 无 WiFi 凭据 → 直接进入配网模式（**这是设计行为，不是故障**）");
        ESP_LOGW(TAG, "    配网步骤：① 手机连热点 ESPNav-AP（密码 espnav1234）");
        ESP_LOGW(TAG, "              ② 浏览器打开 http://192.168.4.1 → 选择/填写手机热点并保存");
        ESP_LOGW(TAG, "    保存后本机会自动连上；**此后每次上电都会先连该热点，不再出现 ESPNav-AP**。");
    }

    static bool s_ap_on = false;
    static bool s_warned_sta_lost = false;   /* 【M16.3】掉线提示只打一次 */
    if (sta_ok) {
        ESP_LOGI(TAG, "已连上热点：IP=%s（不启动配网热点）", wifi_sta_ip_str());
    } else {
        ESP_LOGW(TAG, "未连上热点 → 启动配网热点 ESPNav-AP（配网页 http://192.168.4.1）");
        wifi_ap_start();
        softap_prov_init();
        s_ap_on = true;
    }
    mdns_service_init();
    tcp_server_start();
    ESP_LOGI(TAG, "M14: 通信就绪（STA=%s IP=%s / 配网页 %s / TCP:8899）",
             wifi_sta_is_connected() ? "已连接" : "未连接",
             wifi_sta_ip_str()[0] ? wifi_sta_ip_str() : "-",
             s_ap_on ? "http://192.168.4.1" : "未开启");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        /* 【M16.3】这里**不再**"掉线就补开热点"（原 M14 的设计，实测有严重副作用）：
         *   wifi_ap_start() 内部执行 esp_wifi_set_mode(WIFI_APSTA)，
         *   而**切换 WiFi 模式会重启 WiFi** ⇒ 所有 TCP 连接被 RST
         *   ⇒ APK 看到 Connection reset ⇒ 自动重连 ⇒ 若 STA 又短暂掉线 ⇒ 再次切换 → 无限循环。
         * 现在：热点是否开启**只在启动阶段决定**（连不上才开）；运行中不再切换模式，
         *       因此已建立的 TCP 不会被"配网动作"打断。STA 掉线由 wifi_sta 自己重连。 */
        if (!s_ap_on && !wifi_sta_is_connected() && !s_warned_sta_lost) {
            s_warned_sta_lost = true;
            ESP_LOGW(TAG, "STA 掉线（由 wifi_sta 自动重连；不再切换 WiFi 模式，避免打断已建立的 TCP）");
        }
        ESP_LOGI(TAG, "alive ... STA=%s ip=%s rssi=%d AP_clients_ok",
                 wifi_sta_is_connected() ? "已连接" : "未连接",
                 wifi_sta_ip_str()[0] ? wifi_sta_ip_str() : "-", wifi_sta_rssi());
    }
}
