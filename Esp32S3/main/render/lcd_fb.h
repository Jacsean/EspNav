#pragma once
#include <stdint.h>

#define FB_W 320
#define FB_H 240

/* 帧缓冲（RGB565 native）。优先 PSRAM，失败回退内部 SRAM。 */
void      fb_init(void);
uint16_t *fb_get(void);

void fb_clear(uint16_t color);
void fb_pixel(int x, int y, uint16_t color);
void fb_fill_rect(int x0, int y0, int x1, int y1, uint16_t color);
void fb_line(int x0, int y0, int x1, int y1, uint16_t color);
void fb_dashed_line(int x0, int y0, int x1, int y1, uint16_t color, int dash, int gap);
/* 四点四边形扫描线填充（顺序任意，凸四边形） */
void fb_fill_quad(const int *qx, const int *qy, uint16_t color);
/* 实心三角形（车标） */
void fb_triangle(int cx, int cy, int size, uint16_t color);
/* 推送整屏（逐行 DMA） */
void fb_flush(void);

/* RGB565 便捷色 */
#define RGB565_BLACK 0x0000
#define RGB565_WHITE 0xFFFF
#define RGB565_GRAY  0x7BEF
#define RGB565_DGRAY 0x39E7
#define RGB565_YELLOW 0xFFE0
#define RGB565_GREEN  0x07E0
#define RGB565_RED    0xF800
#define RGB565_BLUE   0x001F
