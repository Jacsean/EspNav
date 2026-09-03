#pragma once
/* 通信承载抽象（协议 V1.10 §0）：WiFi-TCP 默认 / BLE-GATT 备用自动回退 */
typedef enum {
    COMM_CARRIER_NONE = 0,
    COMM_CARRIER_WIFI_TCP,
    COMM_CARRIER_BLE,
} comm_carrier_t;

void comm_if_init(void);
comm_carrier_t comm_if_current(void);
void comm_if_set(comm_carrier_t c);
