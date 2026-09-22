#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 设备配置（协议 V1.10 §3.2/§4.1；出厂缺省与协议 §6 默认值表一致）
 * 【M1/M2 静态图导航】scrim_* / grid_bright / map_area / col_* 都是"ESP 屏显示样式"参数：
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
    uint8_t  grid_bright;     /* 行程图网格亮度 0-100（以 col_grid 为基准色按此比例缩放） */
    uint8_t  map_area;        /* 地图投放区域：0=全屏(320×240) / 1=上半(320×160) */
    /* ---- M2.4：ESP 屏颜色（App 设置里改；0 = 用固件默认色）---- */
    uint16_t col_main;        /* 文字/罗盘/路名/统计/时间/fallback 中心线（默认绿 0x07E0） */
    uint16_t col_track;       /* 行程图轨迹线（默认亮蓝 0x5D9F） */
    uint16_t col_grid;        /* 行程图网格基准色（默认灰绿 0x8450） */
    uint16_t col_road;        /* 路面灰（默认 0x73AE） */
    uint16_t col_car;         /* 车头三角（默认黄 0xFFE0） */
    uint16_t col_hint;        /* 提示/警示行文字（默认黄 0xFFE0） */
    /* ---- M2.5：分光镜 HUD ---- */
    bool     screen_flip;     /* true → 整屏水平镜像：分光镜观察时画面左右翻转，需预先镜像 */
    /* ---- M9：垂直翻转（与水平翻转并列，可独立/同时开启）---- */
    bool     screen_flip_y;   /* true → 整屏垂直镜像：画面上下翻转 */
    /* ---- M7：APK 地图底图总开关（关掉后 ESP 回到原始导航模式，不再贴最后一张图）---- */
    bool     img_on;          /* true=App 正在推底图；false=丢弃底图 → 模板路面/行程图 */
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
/* M2.4：ESP 屏颜色（0 = 用固件默认色） */
void         config_set_col_main(uint16_t v);
void         config_set_col_track(uint16_t v);
void         config_set_col_grid(uint16_t v);
void         config_set_col_road(uint16_t v);
void         config_set_col_car(uint16_t v);
void         config_set_col_hint(uint16_t v);
/* M2.5：分光镜 HUD 整屏水平镜像（App 设置项，默认开） */
void         config_set_screen_flip(bool v);
/* M9：整屏垂直镜像（上下翻转） */
void         config_set_screen_flip_y(bool v);
/* M7：APK 地图底图总开关（App 关闭「推送地图底图」时下发 false） */
void         config_set_img_on(bool v);
