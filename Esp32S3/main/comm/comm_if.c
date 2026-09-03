#include "comm_if.h"
#include "esp_log.h"

static const char *TAG = "comm_if";
static comm_carrier_t s_carrier = COMM_CARRIER_NONE;

void comm_if_init(void) { ESP_LOGI(TAG, "carrier abstract ready (M2 接入 WiFi/BLE)"); }
comm_carrier_t comm_if_current(void) { return s_carrier; }
void comm_if_set(comm_carrier_t c)
{
    s_carrier = c;
    ESP_LOGI(TAG, "carrier -> %s", c == COMM_CARRIER_WIFI_TCP ? "WIFI_TCP" : (c == COMM_CARRIER_BLE ? "BLE" : "NONE"));
}
