#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Wi-Fi STA(客户端)承载：把 ESP32 连到手机热点/路由器，使手机保持外网。
 * 与 wifi_ap.c 的 softAP 并存（APSTA 双模式）；凭据存 NVS，最多 3 组。 */

#define WIFI_STA_MAX_CRED 3

/* 必须在 wifi_ap_start() 之后调用（复用其 nvs_flash/esp_netif/esp_wifi_init） */
void wifi_sta_init(void);

bool wifi_sta_has_saved(void);
int  wifi_sta_cred_count(void);
/* 保存凭据（同 SSID 覆盖密码；已满 3 组则覆盖最旧）；返回是否成功 */
bool wifi_sta_save_credential(const char *ssid, const char *pass);
void wifi_sta_erase_all(void);

bool        wifi_sta_is_connected(void);
const char *wifi_sta_ip_str(void);     /* 已连接返回 "a.b.c.d"，否则 "" */
int         wifi_sta_rssi(void);       /* dBm；未连接返回 0 */
