#include "config.h"
#include <string.h>
#include "esp_log.h"
#include "nvs.h"            /* 【M15】ap_delay_s 的持久化 */

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
    s_cfg.screen_flip_y  = false;/* 【M9】默认不做垂直翻转 */
    s_cfg.ap_delay_s     = 15;   /* 【M15】上电先试连热点 15 秒，超时才开配网热点 */
    s_cfg.audio_vol      = 200;  /* 【M22】语音音量 200%（硬件 100% + 软件 2.0x）；嫌大可调低 */
    /* 【M24】A 组批 2：电量阈值（与原写死值一致，保证行为不变） */
    s_cfg.bat_red_pct    = 15;
    s_cfg.bat_yellow_pct = 40;
    s_cfg.bat_present_mv = 2500;
    s_cfg.img_on         = true; /* 【M7】默认允许底图；App 连接/开始导航时会同步真实开关状态 */
    strncpy(s_cfg.firmware_ver, "V1.4.0", sizeof(s_cfg.firmware_ver) - 1);
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
/* ---- 【M9】整屏垂直镜像 ---- */
void config_set_screen_flip_y(bool v)    { s_cfg.screen_flip_y = v; }

/* ---- 【M15】开机等待热点时间（持久化到 NVS）----
 * 为什么要落 NVS：这个值用在**开机阶段**（决定等多久才开配网热点），
 * 而 App 下发它时必须"已经连上" —— 所以本次运行改不了本次开机，
 * 必须存下来，下次上电才生效。 */
#define NVS_NS      "espnav"
#define NVS_K_APDLY "ap_delay_s"
#define NVS_K_AVOL  "audio_vol"     /* 【M22】语音音量（%），NVS 持久化 */
#define NVS_K_BATR  "bat_red"       /* 【M24】电量红色阈值（%） */
#define NVS_K_BATY  "bat_yellow"    /* 【M24】电量黄色阈值（%） */
#define NVS_K_BATP  "bat_pmv"       /* 【M24】未接电池判据（mV） */

/* 【M24】电量阈值。黄色恒 >= 红色，避免"红黄颠倒"导致颜色判断失去意义。 */
void config_set_bat_red_pct(uint8_t v)
{
    if (v > 100) v = 100;
    s_cfg.bat_red_pct = v;
    if (s_cfg.bat_yellow_pct < v) s_cfg.bat_yellow_pct = v;   /* 红升则黄跟随 */
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_K_BATR, s_cfg.bat_red_pct);
        nvs_set_u8(h, NVS_K_BATY, s_cfg.bat_yellow_pct);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "bat_red_pct=%u（yellow 跟随为 %u）", (unsigned)s_cfg.bat_red_pct,
             (unsigned)s_cfg.bat_yellow_pct);
}

void config_set_bat_yellow_pct(uint8_t v)
{
    if (v > 100) v = 100;
    if (v < s_cfg.bat_red_pct) v = s_cfg.bat_red_pct;          /* 不得低于红色阈值 */
    s_cfg.bat_yellow_pct = v;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_K_BATY, v);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "bat_yellow_pct=%u", (unsigned)v);
}

void config_set_bat_present_mv(uint16_t v)
{
    if (v < 500)  v = 500;
    if (v > 4000) v = 4000;
    s_cfg.bat_present_mv = v;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u16(h, NVS_K_BATP, v);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "bat_present_mv=%u", (unsigned)v);
}

void config_set_audio_vol(uint16_t v)
{
    if (v > 300) v = 300;
    s_cfg.audio_vol = v;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_u16(h, NVS_K_AVOL, v) == ESP_OK) {
            nvs_commit(h);
            ESP_LOGI(TAG, "audio_vol=%u 已存 NVS", (unsigned)v);
        } else {
            ESP_LOGW(TAG, "audio_vol 写入 NVS 失败");
        }
        nvs_close(h);
    }
}

void config_set_ap_delay_s(uint8_t v)
{
    if (v < AP_DELAY_S_MIN) v = AP_DELAY_S_MIN;
    if (v > AP_DELAY_S_MAX) v = AP_DELAY_S_MAX;
    s_cfg.ap_delay_s = v;

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_K_APDLY, v);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "ap_delay_s=%u 已存 NVS（下次上电生效）", (unsigned)v);
    } else {
        ESP_LOGW(TAG, "ap_delay_s 写入 NVS 失败");
    }
}

void config_load_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "NVS 无 espnav 配置，使用默认 ap_delay_s=%u", (unsigned)s_cfg.ap_delay_s);
        return;
    }
    uint8_t v = 0;
    if (nvs_get_u8(h, NVS_K_APDLY, &v) == ESP_OK) {
        if (v < AP_DELAY_S_MIN) v = AP_DELAY_S_MIN;
        if (v > AP_DELAY_S_MAX) v = AP_DELAY_S_MAX;
        s_cfg.ap_delay_s = v;
        ESP_LOGI(TAG, "从 NVS 载入 ap_delay_s=%u 秒", (unsigned)v);
    } else {
        ESP_LOGI(TAG, "NVS 无 ap_delay_s，使用默认 %u 秒", (unsigned)s_cfg.ap_delay_s);
    }
    /* 【M24】电量阈值（读不到就保持默认） */
    uint8_t  br = 0, by = 0;
    uint16_t bp = 0;
    if (nvs_get_u8(h, NVS_K_BATR, &br) == ESP_OK && br <= 100) s_cfg.bat_red_pct = br;
    if (nvs_get_u8(h, NVS_K_BATY, &by) == ESP_OK && by <= 100) s_cfg.bat_yellow_pct = by;
    if (s_cfg.bat_yellow_pct < s_cfg.bat_red_pct) s_cfg.bat_yellow_pct = s_cfg.bat_red_pct;
    if (nvs_get_u16(h, NVS_K_BATP, &bp) == ESP_OK && bp >= 500 && bp <= 4000) s_cfg.bat_present_mv = bp;
    ESP_LOGI(TAG, "电量阈值：红<=%u%% 黄<=%u%% 未接电池<%u mV",
             (unsigned)s_cfg.bat_red_pct, (unsigned)s_cfg.bat_yellow_pct,
             (unsigned)s_cfg.bat_present_mv);

    /* 【M22】语音音量 */
    uint16_t av = 0;
    if (nvs_get_u16(h, NVS_K_AVOL, &av) == ESP_OK && av <= 300) {
        s_cfg.audio_vol = av;
        ESP_LOGI(TAG, "从 NVS 载入 audio_vol=%u%%", (unsigned)av);
    } else {
        ESP_LOGI(TAG, "NVS 无 audio_vol，使用默认 %u%%", (unsigned)s_cfg.audio_vol);
    }
    nvs_close(h);
}
/* ---- 【M7】APK 地图底图总开关 ---- */
void config_set_img_on(bool v)           { s_cfg.img_on = v; }
