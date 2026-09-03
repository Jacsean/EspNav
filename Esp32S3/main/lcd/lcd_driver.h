#pragma once
#include <stdint.h>
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
