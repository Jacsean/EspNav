#include "font.h"
#include "lcd_fb.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "font";
#define FONT_H 16

void font_init(void)
{
    ESP_LOGI(TAG, "点阵字库就绪: %d 字形 (ASCII 8x16 + 汉字 16x16)", font_glyph_count);
}

/* UTF-8 解码（失败返回 0xFFFD 并前进 1 字节） */
static uint32_t utf8_next(const char **pp)
{
    const uint8_t *p = (const uint8_t *)*pp;
    uint32_t cp;
    int extra;
    if (p[0] < 0x80) { cp = p[0]; extra = 0; }
    else if ((p[0] & 0xE0) == 0xC0) { cp = p[0] & 0x1F; extra = 1; }
    else if ((p[0] & 0xF0) == 0xE0) { cp = p[0] & 0x0F; extra = 2; }
    else if ((p[0] & 0xF8) == 0xF0) { cp = p[0] & 0x07; extra = 3; }
    else { *pp = (const char *)(p + 1); return 0xFFFD; }
    for (int i = 1; i <= extra; i++) {
        if ((p[i] & 0xC0) != 0x80) { *pp = (const char *)(p + 1); return 0xFFFD; }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *pp = (const char *)(p + extra + 1);
    return cp;
}

static const font_glyph_t *find_glyph(uint32_t cp)
{
    int lo = 0, hi = font_glyph_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t v = font_glyphs[mid].cp;
        if (v == cp) return &font_glyphs[mid];
        if (v < cp) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

int font_draw_text(int x, int y, const char *utf8, uint16_t color)
{
    const char *p = utf8;
    int cx = x;
    while (*p) {
        uint32_t cp = utf8_next(&p);
        if (cp == 0) break;
        const font_glyph_t *g = find_glyph(cp);
        if (g) {
            int nb = (g->w + 7) / 8;
            for (int yy = 0; yy < FONT_H; yy++) {
                for (int xx = 0; xx < g->w; xx++) {
                    uint8_t byte = font_bits[g->off + (uint32_t)yy * nb + (uint32_t)(xx / 8)];
                    if (byte & (0x80 >> (xx % 8))) fb_pixel(cx + xx, y + yy, color);
                }
            }
            cx += g->w;
        } else {
            /* 缺字占位：空心方块 */
            for (int i = 0; i < 16; i++) {
                fb_pixel(cx + i, y, color);
                fb_pixel(cx + i, y + 15, color);
                fb_pixel(cx, y + i, color);
                fb_pixel(cx + 15, y + i, color);
            }
            cx += 16;
        }
    }
    return cx;
}

int font_text_width(const char *utf8)
{
    const char *p = utf8;
    int w = 0;
    while (*p) {
        uint32_t cp = utf8_next(&p);
        if (cp == 0) break;
        const font_glyph_t *g = find_glyph(cp);
        w += g ? g->w : 16;
    }
    return w;
}

void font_clip_utf8(const char *utf8, int max_px, char *out, int outsz)
{
    const char *p = utf8;
    int used = 0, oi = 0;
    int budget = max_px - 16;              /* 给末尾 … 预留一个全角 */
    while (*p && oi < outsz - 1) {
        const char *save = p;
        uint32_t cp = utf8_next(&p);
        if (cp == 0) break;
        const font_glyph_t *g = find_glyph(cp);
        int gw = g ? g->w : 16;
        if (used + gw > budget) {          /* 截断：补 … */
            const char *dots = "…";   /* U+2026 */
            while (*dots && oi < outsz - 1) out[oi++] = *dots++;
            break;
        }
        int n = (int)(p - save);
        for (int i = 0; i < n && oi < outsz - 1; i++) out[oi++] = save[i];
        used += gw;
    }
    out[oi] = 0;
}

/* 与 font_draw_text 相同，但只绘制落在 [clip_x0, clip_x1] 内的像素（滚动文本用） */
int font_draw_text_clip(int x, int y, const char *utf8, uint16_t color, int clip_x0, int clip_x1)
{
    const char *p = utf8;
    int cx = x;
    while (*p) {
        uint32_t cp = utf8_next(&p);
        if (cp == 0) break;
        const font_glyph_t *g = find_glyph(cp);
        int w = g ? (int)g->w : 16;
        if (cx + w >= clip_x0 && cx <= clip_x1) {          /* 只处理与裁剪区相交的字 */
            if (g) {
                int nb = (g->w + 7) / 8;
                for (int yy = 0; yy < FONT_H; yy++) {
                    for (int xx = 0; xx < g->w; xx++) {
                        int px = cx + xx;
                        if (px < clip_x0 || px > clip_x1) continue;
                        uint8_t byte = font_bits[g->off + (uint32_t)yy * nb + (uint32_t)(xx / 8)];
                        if (byte & (0x80 >> (xx % 8))) fb_pixel(px, y + yy, color);
                    }
                }
            } else {
                for (int i = 0; i < 16; i++) {
                    int px = cx + i;
                    if (px >= clip_x0 && px <= clip_x1) {
                        fb_pixel(px, y, color);
                        fb_pixel(px, y + 15, color);
                    }
                }
                if (cx >= clip_x0 && cx <= clip_x1)
                    for (int i = 0; i < 16; i++) fb_pixel(cx, y + i, color);
            }
        }
        cx += w;
    }
    return cx;
}

/* 按整数倍放大绘制（每像素放大为 scale×scale 方块） */
int font_draw_text_scaled(int x, int y, const char *utf8, uint16_t color, int scale)
{
    const char *p = utf8;
    int cx = x;
    if (scale < 1) scale = 1;
    while (*p) {
        uint32_t cp = utf8_next(&p);
        if (cp == 0) break;
        const font_glyph_t *g = find_glyph(cp);
        int gw = g ? (int)g->w : 16;
        if (g) {
            int nb = (g->w + 7) / 8;
            for (int yy = 0; yy < FONT_H; yy++) {
                for (int xx = 0; xx < g->w; xx++) {
                    uint8_t byte = font_bits[g->off + (uint32_t)yy * nb + (uint32_t)(xx / 8)];
                    if (byte & (0x80 >> (xx % 8))) {
                        int bx = cx + xx * scale, by = y + yy * scale;
                        for (int dy = 0; dy < scale; dy++)
                            for (int dx = 0; dx < scale; dx++)
                                fb_pixel(bx + dx, by + dy, color);
                    }
                }
            }
        }
        cx += gw * scale;
    }
    return cx;
}
