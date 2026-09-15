#include "lcd_fb.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "lcd_fb";
static uint16_t *s_fb = NULL;
static bool s_in_psram = false;

void lcd_ili9341_flush(const uint16_t *fb, int w, int h);   /* ili9341.c */

void fb_init(void)
{
    if (s_fb) return;
    s_fb = (uint16_t *)heap_caps_malloc(FB_W * FB_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_fb) { s_in_psram = true; }
    else {
        s_fb = (uint16_t *)heap_caps_malloc(FB_W * FB_H * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    ESP_LOGI(TAG, "framebuffer %dx%d (%d bytes) in %s", FB_W, FB_H, FB_W * FB_H * 2,
             s_in_psram ? "PSRAM" : "SRAM");
}

uint16_t *fb_get(void) { return s_fb; }

void fb_clear(uint16_t color)
{
    if (!s_fb) return;
    for (int i = 0; i < FB_W * FB_H; i++) s_fb[i] = color;
}

void fb_pixel(int x, int y, uint16_t color)
{
    if (!s_fb || x < 0 || y < 0 || x >= FB_W || y >= FB_H) return;
    s_fb[y * FB_W + x] = color;
}

void fb_fill_rect(int x0, int y0, int x1, int y1, uint16_t color)
{
    if (!s_fb) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= FB_W) x1 = FB_W - 1;
    if (y1 >= FB_H) y1 = FB_H - 1;
    for (int y = y0; y <= y1; y++) {
        uint16_t *row = &s_fb[y * FB_W];
        for (int x = x0; x <= x1; x++) row[x] = color;
    }
}

void fb_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        fb_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void fb_dashed_line_off(int x0, int y0, int x1, int y1, uint16_t color, int dash, int gap, float offset)
{
    int dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf((float)(dx * dx + dy * dy));
    if (len < 1.0f) return;
    float ux = dx / len, uy = dy / len;
    float cycle = (float)(dash + gap);
    if (cycle < 1.0f) cycle = 1.0f;
    float phase = fmodf(offset, cycle);
    if (phase < 0) phase += cycle;
    for (float pos = -phase; pos < len; pos += cycle) {
        float a = pos < 0 ? 0.0f : pos;
        float e = pos + dash;
        if (e > len) e = len;
        if (e <= a) continue;
        fb_line(x0 + (int)(ux * a), y0 + (int)(uy * a),
                x0 + (int)(ux * e), y0 + (int)(uy * e), color);
    }
}

void fb_dashed_line(int x0, int y0, int x1, int y1, uint16_t color, int dash, int gap)
{
    fb_dashed_line_off(x0, y0, x1, y1, color, dash, gap, 0.0f);
}

void fb_fill_quad(const int *qx, const int *qy, uint16_t color)
{
    int ymin = qy[0], ymax = qy[0];
    for (int i = 1; i < 4; i++) {
        if (qy[i] < ymin) ymin = qy[i];
        if (qy[i] > ymax) ymax = qy[i];
    }
    if (ymin < 0) ymin = 0;
    if (ymax > FB_H - 1) ymax = FB_H - 1;
    for (int y = ymin; y <= ymax; y++) {
        float xs[4]; int n = 0;
        for (int i = 0; i < 4; i++) {
            int j = (i + 1) % 4;
            int ya = qy[i], yb = qy[j];
            if ((ya <= y && yb > y) || (yb <= y && ya > y)) {
                float t = (float)(y - ya) / (float)(yb - ya);
                xs[n++] = qx[i] + t * (float)(qx[j] - qx[i]);
            }
        }
        if (n < 2) continue;
        float lo = xs[0], hi = xs[0];
        for (int i = 1; i < n; i++) { if (xs[i] < lo) lo = xs[i]; if (xs[i] > hi) hi = xs[i]; }
        int x0 = (int)lo, x1 = (int)hi;
        if (x0 < 0) x0 = 0;
        if (x1 > FB_W - 1) x1 = FB_W - 1;
        uint16_t *row = &s_fb[y * FB_W];
        for (int x = x0; x <= x1; x++) row[x] = color;
    }
}

void fb_triangle(int cx, int cy, int size, uint16_t color)
{
    int qx[4] = { cx, cx - size * 7 / 10, cx + size * 7 / 10, cx };
    int qy[4] = { cy - size, cy + size * 6 / 10, cy + size * 6 / 10, cy - size };
    /* 三角形：用三点版本的扫描线（复用 quad 的第 4 点=第 1 点） */
    int tx[4] = { qx[0], qx[1], qx[2], qx[0] };
    int ty[4] = { qy[0], qy[1], qy[2], qy[0] };
    fb_fill_quad(tx, ty, color);
}

void fb_flush(void)
{
    if (!s_fb) return;
    lcd_ili9341_flush(s_fb, FB_W, FB_H);
}

void fb_fill_poly(const int *xs, const int *ys, int n, uint16_t color)
{
    if (!s_fb || !xs || !ys || n < 3) return;
    int ymin = ys[0], ymax = ys[0];
    for (int i = 1; i < n; i++) { if (ys[i] < ymin) ymin = ys[i]; if (ys[i] > ymax) ymax = ys[i]; }
    if (ymin < 0) ymin = 0;
    if (ymax > FB_H - 1) ymax = FB_H - 1;
    int xbuf[128];
    for (int y = ymin; y <= ymax; y++) {
        int cnt = 0;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            int ya = ys[i], yb = ys[j];
            if ((ya <= y && yb > y) || (yb <= y && ya > y)) {
                float t = (float)(y - ya) / (float)(yb - ya);
                int x = xs[i] + (int)(t * (float)(xs[j] - xs[i]));
                if (cnt < 128) xbuf[cnt++] = x;
            }
        }
        if (cnt < 2) continue;
        int lo = xbuf[0], hi = xbuf[0];
        for (int i = 1; i < cnt; i++) { if (xbuf[i] < lo) lo = xbuf[i]; if (xbuf[i] > hi) hi = xbuf[i]; }
        if (lo < 0) lo = 0;
        if (hi > FB_W - 1) hi = FB_W - 1;
        uint16_t *row = &s_fb[y * FB_W];
        for (int x = lo; x <= hi; x++) row[x] = color;
    }
}

void fb_ellipse(int cx, int cy, int a, int b, uint16_t color)
{
    int xs[33], ys[33];
    int n = 32;
    for (int i = 0; i < n; i++) {
        float th = 6.2831853f * (float)i / (float)n;
        xs[i] = cx + (int)(a * cosf(th));
        ys[i] = cy + (int)(b * sinf(th));
    }
    fb_fill_poly(xs, ys, n, color);
}
