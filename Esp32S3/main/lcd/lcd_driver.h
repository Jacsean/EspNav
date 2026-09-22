#pragma once
#include <stdint.h>
#include <stdbool.h>
/* LCD 驱动 IC 抽象层（固件规划 §3.1）：ILI9341 默认；同规格 SPI LCD 可换实现 */
typedef struct {
    void (*init)(void);
    void (*set_window)(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    void (*push_pixels)(const uint16_t *data, uint32_t n);
    void (*backlight)(uint8_t pct);   /* 0-100 */
} lcd_driver_ops_t;

void lcd_driver_init(void);
const lcd_driver_ops_t *lcd_driver_ops(void);
/* 基础绘图原语（M1 实现）：填充矩形/画线/虚线/多边形/文字 */
void lcd_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);

/* M1 点亮探测：全屏色条轮换（红/绿/蓝/白/黑），串口同步打印 */
void lcd_probe_color_cycle(void);

/* 【M1.7】整屏水平镜像（分光镜 HUD 用）：true 时送屏前把每行像素左右倒序。
 * 实现在 ili9341.c；由 SET_CONFIG.screen_flip 设置（App 侧默认开）。 */
void lcd_ili9341_set_flip_x(bool on);
/* 【M9】整屏垂直镜像（上下翻转）：true 时按行倒序送屏。由 SET_CONFIG.screen_flip_y 设置。 */
void lcd_ili9341_set_flip_y(bool on);
