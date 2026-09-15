#include "mdns_service.h"
#include "esp_log.h"

static const char *TAG = "mdns_service";

void mdns_service_init(void)
{
    /* ESP-IDF 6.1 起 mDNS 不再是内置组件：需 `idf.py add-dependency "espressif/mdns"`。
     * 未安装时不引用其头文件（保证可编译），仅提示可用 IP 直连。 */
    ESP_LOGW(TAG, "mDNS 未启用（需 espressif/mdns 组件）；请用 IP 直连 8899 或配网页");
}
