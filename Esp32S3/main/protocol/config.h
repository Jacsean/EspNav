#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 设备配置（协议 V1.10 §3.2/§4.1；出厂缺省与协议 §6 默认值表一致）
 * 【M1 静态图导航】scrim_* / grid_bright / map_area 是"地图底图之上的叠加层可读性"参数：
 *   只由 App 下发（App 侧持久化在 espnav_config.json 里），固件不落 NVS —— 与既有 4 项一致。 */
typedef struct {
    uint8_t  lcd_brightness;  /* 0-100 */
    uint8_t  dash_speed;      /* 像素/秒 */
    bool     anim_enable;
    uint8_t  popup_timeout;   /* 秒；0=不自动关闭 */
    /* ---- M1：叠加层可读性（均有 App 设置项）---- */
    bool     scrim_on;        /* 半透明暗底衬总开关 */
    uint8_t  scrim_compass;   /* 罗盘底衬透明度 0-100（越大越透、地图越明显） */
    uint8_t  scrim_text;      /* 文字底衬透明度（路名/hint/距离/左下统计） */
    uint8_t  scrim_route;     /* 行程图底衬透明度 */
    uint8_t  scrim_clock;     /* 时间底衬透明度 */
    uint8_t  grid_bright;     /* 行程图网格亮度 0-100（主格线每 50px 再亮一档） */
    uint8_t  map_area;        /* 地图投放区域：0=全屏(320×240) / 1=上半(320×160) */
    char     firmware_ver[16];
} espnav_config_t;

void         config_init(void);
const espnav_config_t *config_get(void);
void         config_set_brightness(uint8_t v);
void         config_set_dash_speed(uint8_t v);
void         config_set_anim_enable(bool v);
void         config_set_popup_timeout(uint8_t v);
/* M1：叠加层可读性（透明度语义：0=全黑底衬，100=不铺底衬） */
void         config_set_scrim_on(bool v);
void         config_set_scrim_compass(uint8_t v);
void         config_set_scrim_text(uint8_t v);
void         config_set_scrim_route(uint8_t v);
void         config_set_scrim_clock(uint8_t v);
void         config_set_grid_bright(uint8_t v);
void         config_set_map_area(uint8_t v);
