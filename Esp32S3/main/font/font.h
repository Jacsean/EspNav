#pragma once
#include <stdint.h>

/* 点阵字库（Unicode 子集索引，二分查找）：ASCII 8x16 + 汉字/符号 16x16
 * 数据由 tools/gen_font.py 生成（font_data.c）。缺字显示空心方块占位。 */
typedef struct {
    uint32_t cp;    /* Unicode 码点 */
    uint8_t  w;     /* 字宽：8(半角) 或 16(全角) */
    uint32_t off;   /* font_bits 内偏移 */
} font_glyph_t;

extern const font_glyph_t font_glyphs[];
extern const int font_glyph_count;
extern const uint8_t font_bits[];

void font_init(void);
/* 绘制 UTF-8 文本，返回结束 x */
int  font_draw_text(int x, int y, const char *utf8, uint16_t color);
/* 文本像素宽度 */
int  font_text_width(const char *utf8);
/* 按最大像素宽度截断（超出末尾追加 …） */
void font_clip_utf8(const char *utf8, int max_px, char *out, int outsz);
