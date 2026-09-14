#include "config.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "config";
static espnav_config_t s_cfg;

void config_init(void)
{
    s_cfg.lcd_brightness = 80;
    s_cfg.dash_speed     = 40;   /* 骑行观感调慢（原 60 偏快），可经 SET_CONFIG 调整 */
    s_cfg.anim_enable    = true;
    s_cfg.popup_timeout  = 5;
    strncpy(s_cfg.firmware_ver, "V0.0.1", sizeof(s_cfg.firmware_ver) - 1);
    ESP_LOGI(TAG, "config default ready");
}

const espnav_config_t *config_get(void) { return &s_cfg; }
void config_set_brightness(uint8_t v)    { if (v <= 100) s_cfg.lcd_brightness = v; }
void config_set_dash_speed(uint8_t v)    { s_cfg.dash_speed = v; }
void config_set_anim_enable(bool v)      { s_cfg.anim_enable = v; }
void config_set_popup_timeout(uint8_t v) { s_cfg.popup_timeout = v; }
