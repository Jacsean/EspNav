#pragma once
/* 配网页：手机连 ESPNav-AP -> 浏览器打开 http://192.168.4.1
 * 可搜索附近 WiFi、填写并保存凭据（保存后 ESP32 自动以 STA 连接）。 */
void softap_prov_init(void);
