#pragma once
/* M2 测试期：ESP32 自开 softAP（无需路由器/凭据）。
 * 正式形态见协议 §0：softAP 仅用于配网；数据走 STA 连热点/路由。 */
void wifi_ap_start(void);
