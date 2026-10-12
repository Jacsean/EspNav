#pragma once
/* M2 测试期：ESP32 自开 softAP（无需路由器/凭据）。
 * 正式形态见协议 §0：softAP 仅用于配网；数据走 STA 连热点/路由。 */
/* 【M14】公共 Wi-Fi 初始化（nvs / netif / event loop / esp_wifi_init）。
 * 不开热点、不启动 Wi-Fi —— 让 STA 能先跑（原实现把这些塞在 wifi_ap_start() 里，
 * 导致"必须先起热点才能连热点"）。 */
void wifi_common_init(void);

/* 启动配网热点（SoftAP，SSID=ESPNav-AP）。**按需调用**：默认启动流程里只有
 * "连不上已保存热点" 时才会调用；运行中 STA 掉线时也会补调。幂等。 */
void wifi_ap_start(void);
