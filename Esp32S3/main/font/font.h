#pragma once
#include <stdint.h>
/* 16x16 点阵字库（GB2312）+ 8x16 ASCII（固件规划 §3.4）。骨架：接口占位。 */
void font_init(void);
int  font_draw_text(uint16_t x, uint16_t y, const char *utf8, uint16_t color);
