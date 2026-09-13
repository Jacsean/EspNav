#include "render_nav.h"
#include "lcd_fb.h"
#include <math.h>
#include "esp_log.h"

static const char *TAG = "render_nav";

/* 与 nav_sim_v2.html 冻结基准一致：主视图 320x160 / 近宽150 远宽36 / y近130 远28 */
#define NAV_CX        160
#define NAV_NEAR_Y    130
#define NAV_FAR_Y     28
#define NAV_NEAR_HALF 75      /* 近宽 150 */
#define NAV_FAR_HALF  18      /* 远宽 36  */
#define ROAD_GRAY     0x73AE  /* #777777 -> RGB565 */
#define PATH_GREEN    0x07E0

void render_nav_init(void)
{
    fb_init();
}

void render_nav_demo(void)
{
    fb_clear(RGB565_BLACK);

    /* 主视图透视梯形条带 */
    int qx[4] = { NAV_CX - NAV_NEAR_HALF, NAV_CX + NAV_NEAR_HALF,
                  NAV_CX + NAV_FAR_HALF,  NAV_CX - NAV_FAR_HALF };
    int qy[4] = { NAV_NEAR_Y, NAV_NEAR_Y, NAV_FAR_Y, NAV_FAR_Y };
    fb_fill_quad(qx, qy, ROAD_GRAY);

    /* 两侧边界虚线 + 车道中线虚线 */
    fb_dashed_line(NAV_CX - NAV_NEAR_HALF, NAV_NEAR_Y, NAV_CX - NAV_FAR_HALF, NAV_FAR_Y, RGB565_WHITE, 8, 6);
    fb_dashed_line(NAV_CX + NAV_NEAR_HALF, NAV_NEAR_Y, NAV_CX + NAV_FAR_HALF, NAV_FAR_Y, RGB565_WHITE, 8, 6);
    fb_dashed_line(NAV_CX, NAV_NEAR_Y, NAV_CX, NAV_FAR_Y, RGB565_WHITE, 5, 7);

    /* 已行驶 / 未行驶路径（绿） */
    fb_line(NAV_CX, NAV_NEAR_Y, NAV_CX, 110, PATH_GREEN);
    fb_line(NAV_CX, 110, NAV_CX, NAV_FAR_Y, PATH_GREEN);

    /* 车辆三角（黄） */
    fb_triangle(NAV_CX, 110, 14, RGB565_YELLOW);

    /* 版面示意：顶部信息带 + 左下统计区 + 右下 overview 区 */
    fb_fill_rect(0, 0, FB_W - 1, 1, RGB565_GREEN);
    fb_line(0, 160, FB_W - 1, 160, RGB565_DGRAY);
    fb_line(220, 160, 220, FB_H - 1, RGB565_DGRAY);
    fb_fill_rect(222, 162, FB_W - 3, FB_H - 3, RGB565_BLACK);

    fb_flush();
    ESP_LOGI(TAG, "demo strip rendered (near150/far36) and flushed");
}

/* 帧渲染（M2）：按 centerLine 首末点生成透视梯形（近宽150/远宽36，与 v2 genPerspectiveRoadTrapezoid 一致） */
static void quad_from_centerline(const nav_frame_t *f, int nearHalf, int farHalf, int *qx, int *qy)
{
    float dx = (float)(f->center_line[f->center_n - 1].x - f->center_line[0].x);
    float dy = (float)(f->center_line[f->center_n - 1].y - f->center_line[0].y);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.1f) { qx[0]=qx[1]=qx[2]=qx[3]=f->center_line[0].x; qy[0]=qy[1]=qy[2]=qy[3]=f->center_line[0].y; return; }
    float nx = -dy / len, ny = dx / len;
    int x0 = f->center_line[0].x, y0 = f->center_line[0].y;
    int x1 = f->center_line[f->center_n - 1].x, y1 = f->center_line[f->center_n - 1].y;
    qx[0] = x0 + (int)(nx * nearHalf); qy[0] = y0 + (int)(ny * nearHalf);
    qx[1] = x0 - (int)(nx * nearHalf); qy[1] = y0 - (int)(ny * nearHalf);
    qx[2] = x1 - (int)(nx * farHalf);  qy[2] = y1 - (int)(ny * farHalf);
    qx[3] = x1 + (int)(nx * farHalf);  qy[3] = y1 + (int)(ny * farHalf);
}

void render_nav_frame(const nav_frame_t *f)
{
    if (!f || !f->valid) return;
    fb_clear(RGB565_BLACK);
    fb_fill_rect(0, 0, FB_W - 1, 1, RGB565_GREEN);          /* 顶部信息带示意 */
    fb_line(0, 160, FB_W - 1, 160, RGB565_DGRAY);
    fb_line(220, 160, 220, FB_H - 1, RGB565_DGRAY);

    if (f->center_n >= 2) {
        int qx[4], qy[4];
        quad_from_centerline(f, NAV_NEAR_HALF, NAV_FAR_HALF, qx, qy);
        fb_fill_quad(qx, qy, ROAD_GRAY);
        /* 边界虚线（梯形左右边 + 顶点法线四角） */
        fb_dashed_line(qx[0], qy[0], qx[3], qy[3], RGB565_WHITE, 8, 6);
        fb_dashed_line(qx[1], qy[1], qx[2], qy[2], RGB565_WHITE, 8, 6);
        /* 车道中线虚线（沿 centerLine 折线） */
        for (int i = 0; i + 1 < f->center_n; i++) {
            fb_dashed_line(f->center_line[i].x, f->center_line[i].y,
                           f->center_line[i + 1].x, f->center_line[i + 1].y, RGB565_WHITE, 5, 7);
        }
    }
    /* 已行驶 / 未行驶路径（绿） */
    for (int i = 0; i + 1 < f->past_n; i++)
        fb_line(f->past_center[i].x, f->past_center[i].y, f->past_center[i + 1].x, f->past_center[i + 1].y, PATH_GREEN);
    for (int i = 0; i + 1 < f->route_n; i++)
        fb_line(f->route_center[i].x, f->route_center[i].y, f->route_center[i + 1].x, f->route_center[i + 1].y, PATH_GREEN);
    /* 车辆光标 */
    if (f->pos_valid) fb_triangle(f->pos.x, f->pos.y, 14, RGB565_YELLOW);

    fb_flush();
}
