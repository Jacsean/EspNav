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
    /* M1 默认档位 = nav_compass.html 演示里确认的值（App 设置页可随时改，固件只是初始值） */
    s_cfg.scrim_on       = true;
    s_cfg.scrim_compass  = 65;
    s_cfg.scrim_text     = 65;
    s_cfg.scrim_route    = 40;   /* 行程图底衬比其它更实一点：网格线本身偏暗 */
    s_cfg.scrim_clock    = 65;
    s_cfg.grid_bright    = 55;
    s_cfg.map_area       = 0;    /* 默认全屏投放 */
    /* M2.4 颜色：0 = 用渲染端默认色（App 未设置时不影响既有观感） */
    s_cfg.col_main       = 0;
    s_cfg.col_track      = 0;
    s_cfg.col_grid       = 0;
    s_cfg.col_road       = 0;
    s_cfg.col_car        = 0;
    s_cfg.col_hint       = 0;
    s_cfg.screen_flip    = true; /* 【M2.5】默认水平镜像（分光镜 HUD 场景）*/
    s_cfg.img_on         = true; /* 【M7】默认允许底图；App 连接/开始导航时会同步真实开关状态 */
    strncpy(s_cfg.firmware_ver, "V1.2.0", sizeof(s_cfg.firmware_ver) - 1);
    ESP_LOGI(TAG, "config default ready");
}

const espnav_config_t *config_get(void) { return &s_cfg; }
void config_set_brightness(uint8_t v)    { if (v <= 100) s_cfg.lcd_brightness = v; }
void config_set_dash_speed(uint8_t v)    { s_cfg.dash_speed = v; }
void config_set_anim_enable(bool v)      { s_cfg.anim_enable = v; }
void config_set_popup_timeout(uint8_t v) { s_cfg.popup_timeout = v; }
/* ---- M1：叠加层可读性 ---- */
void config_set_scrim_on(bool v)         { s_cfg.scrim_on = v; }
void config_set_scrim_compass(uint8_t v) { if (v <= 100) s_cfg.scrim_compass = v; }
void config_set_scrim_text(uint8_t v)    { if (v <= 100) s_cfg.scrim_text = v; }
void config_set_scrim_route(uint8_t v)   { if (v <= 100) s_cfg.scrim_route = v; }
void config_set_scrim_clock(uint8_t v)   { if (v <= 100) s_cfg.scrim_clock = v; }
void config_set_grid_bright(uint8_t v)   { if (v <= 100) s_cfg.grid_bright = v; }
void config_set_map_area(uint8_t v)      { s_cfg.map_area = (v == 0) ? 0 : 1; }
/* ---- M2.4：ESP 屏颜色（0 = 用固件默认色）---- */
void config_set_col_main(uint16_t v)     { s_cfg.col_main = v; }
void config_set_col_track(uint16_t v)    { s_cfg.col_track = v; }
void config_set_col_grid(uint16_t v)     { s_cfg.col_grid = v; }
void config_set_col_road(uint16_t v)     { s_cfg.col_road = v; }
void config_set_col_car(uint16_t v)      { s_cfg.col_car = v; }
void config_set_col_hint(uint16_t v)     { s_cfg.col_hint = v; }
/* ---- M2.5：分光镜 HUD 整屏水平镜像 ---- */
void config_set_screen_flip(bool v)      { s_cfg.screen_flip = v; }
/* ---- 【M7】APK 地图底图总开关 ---- */
void config_set_img_on(bool v)           { s_cfg.img_on = v; }
