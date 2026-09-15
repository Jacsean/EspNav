#pragma once
/* mDNS：espnav.local / _espnav._tcp:8899
 * 注意：ESP-IDF 6.1 不再内置 mdns 组件，需 `idf.py add-dependency "espressif/mdns"` 后实现。 */
void mdns_service_init(void);
