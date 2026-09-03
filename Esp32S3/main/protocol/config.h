#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 设备配置（协议 V1.10 §3.2/§4.1；出厂缺省与协议 §6 默认值表一致） */
typedef struct {
    uint8_t  lcd_brightness;  /* 0-100 */
    uint8_t  dash_speed;      /* 像素/秒 */
    bool     anim_enable;
    uint8_t  popup_timeout;   /* 秒；0=不自动关闭 */
    char     firmware_ver[16];
} espnav_config_t;

void         config_init(void);
const espnav_config_t *config_get(void);
void         config_set_brightness(uint8_t v);
void         config_set_dash_speed(uint8_t v);
void         config_set_anim_enable(bool v);
void         config_set_popup_timeout(uint8_t v);
