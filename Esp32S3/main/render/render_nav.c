#include "render_nav.h"
#include "lcd_fb.h"
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
