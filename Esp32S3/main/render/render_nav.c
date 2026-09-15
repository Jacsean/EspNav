#include "render_nav.h"
#include "lcd_fb.h"
#include <math.h>
#include "esp_log.h"
#include "config.h"
#include "font.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "render_nav";

/* 临时二分开关：1=渲染文字层；0=跳过文字（用于定位黑屏/崩溃是否由文字渲染引起） */
#define RENDER_TEXT 1   /* 文字渲染已恢复（黑屏根因=CS 控制，与文字无关） */

/* 与 nav_sim_v2.html 冻结基准一致：主视图 320x160 / 近宽150 远宽36 / y近130 远28 */
#define NAV_CX        160
#define NAV_NEAR_Y    144     /* 路面下边沿下移一个车身（2026-09-14 反馈） */
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
    /* 内置直行样例帧：交给显示任务周期渲染（含虚线动画与文字），
     * 开机即验证 帧->渲染->动画->文字 整条链路，不依赖外部发帧。 */
    static nav_frame_t f;
    memset(&f, 0, sizeof(f));
    f.heading = 0;
    f.turn_dist = 460;
    snprintf(f.hint, sizeof(f.hint), "%s", "前方460米直行");
    f.total_dist = 8200;
    f.progress_pct = 34;
    f.elapsed_min = 28;
    snprintf(f.eta_time, sizeof(f.eta_time), "%s", "14:27");

    static const int pts[5][2] = { {160, NAV_NEAR_Y}, {160, 118}, {160, 86}, {160, 54}, {160, 30} };
    for (int i = 0; i < 5; i++) {
        f.center_line[i].x = (int16_t)pts[i][0];
        f.center_line[i].y = (int16_t)pts[i][1];
    }
    f.center_n = 5;
    f.past_n = 2;
    f.past_center[0] = f.center_line[0];
    f.past_center[1] = f.center_line[1];
    f.route_n = 4;
    for (int i = 0; i < 4; i++) f.route_center[i] = f.center_line[i + 1];
    f.pos.x = 160; f.pos.y = 110; f.pos_valid = true;

    f.heading = 45;                                  /* 罗盘可动 */
    static const int ov[4][2] = { {8,6}, {22,19}, {46,14}, {62,26} };
    for (int i = 0; i < 4; i++) {
        f.overview[i].x = (int16_t)ov[i][0];
        f.overview[i].y = (int16_t)ov[i][1];
    }
    f.overview_n = 4;
    f.overview_dot.x = 22; f.overview_dot.y = 19; f.overview_dot_valid = true;
    f.valid = true;

    render_nav_set_frame(&f);
    ESP_LOGI(TAG, "demo frame set (built-in straight sample, rendered by display task)");
}

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

static nav_frame_t s_cur;
static bool s_have = false;
static float s_anim = 0.0f;

void render_nav_set_frame(const nav_frame_t *f)
{
    if (!f || !f->valid) return;
    s_cur = *f;
    s_have = true;
}

/* 罗盘（顶部右侧）：8 方位标签随 heading 平移 + 中央红色车头箭头（车头朝上） */
static void draw_compass(const nav_frame_t *f)
{
    static const char *labels[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    const int bx = 235, spread = 75, y = 4;
    for (int i = 0; i < 8; i++) {
        int off = (((((i * 45) - f->heading + 360) % 360) - 180) * spread) / 180;
        int tx = bx + off - font_text_width(labels[i]) / 2;
        if (tx > bx - spread && tx < bx + spread && tx > -20 && tx < 312) {
            font_draw_text(tx, y, labels[i], PATH_GREEN);
        }
    }
    fb_triangle(bx, 28, 6, RGB565_RED);          /* 红色车头箭头（固定朝上） */
}

/* 行程图（右下 overview）：网格 + 路径 + 当前位置点
 * 协议 §3.1 第 5 条：坐标为相对小地图左上角像素坐标；渲染端内边距 8、y 翻转(40-y)。 */
static void draw_overview(const nav_frame_t *f)
{
    const int ax = 220, ay = 160, aw = 100, ah = 80;
    const uint16_t grid = 0x2104;
    for (int x = ax; x < ax + aw; x += 10) fb_line(x, ay, x, ay + ah - 1, grid);
    for (int y = ay; y < ay + ah; y += 10) fb_line(ax, y, ax + aw - 1, y, grid);
    for (int i = 0; i + 1 < f->overview_n; i++) {
        int x0 = ax + 8 + f->overview[i].x,     y0 = ay + 8 + (40 - f->overview[i].y);
        int x1 = ax + 8 + f->overview[i + 1].x, y1 = ay + 8 + (40 - f->overview[i + 1].y);
        fb_line(x0, y0, x1, y1, PATH_GREEN);
    }
    if (f->overview_dot_valid) {
        int dx = ax + 8 + f->overview_dot.x, dy = ay + 8 + (40 - f->overview_dot.y);
        fb_fill_rect(dx - 2, dy - 2, dx + 2, dy + 2, RGB565_YELLOW);
    }
    font_draw_text(ax + 80, ay + 2, "北", PATH_GREEN);   /* 小地图北向标记（汉字，与 HTML V2 一致） */
}

static void draw_frame(const nav_frame_t *f, float anim)
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
        /* 边界虚线（带流动相位：offset 增大 -> 向近端/下方滚动） */
        fb_dashed_line_off(qx[0], qy[0], qx[3], qy[3], RGB565_WHITE, 8, 6, anim);
        fb_dashed_line_off(qx[1], qy[1], qx[2], qy[2], RGB565_WHITE, 8, 6, anim);
        /* 车道中线虚线（沿 centerLine 折线） */
        for (int i = 0; i + 1 < f->center_n; i++) {
            fb_dashed_line_off(f->center_line[i].x, f->center_line[i].y,
                               f->center_line[i + 1].x, f->center_line[i + 1].y,
                               RGB565_WHITE, 5, 7, anim);
        }
    }
    /* 已行驶 / 未行驶路径（绿） */
    for (int i = 0; i + 1 < f->past_n; i++)
        fb_line(f->past_center[i].x, f->past_center[i].y, f->past_center[i + 1].x, f->past_center[i + 1].y, PATH_GREEN);
    for (int i = 0; i + 1 < f->route_n; i++)
        fb_line(f->route_center[i].x, f->route_center[i].y, f->route_center[i + 1].x, f->route_center[i + 1].y, PATH_GREEN);
    /* 车辆光标 */
    if (f->pos_valid) fb_triangle(f->pos.x, f->pos.y, 14, RGB565_YELLOW);

#if RENDER_TEXT
    /* 罗盘 + 行程图（不依赖 RENDER_TEXT 开关） */
    draw_compass(f);
    draw_overview(f);

    /* ---- 文字层（hint / 距离 / 统计）---- */
    {
        static char buf[64];                 /* static：避免显示任务栈压力 */
        font_clip_utf8(f->hint, 9 * 16, buf, sizeof(buf));      /* hint ≤9 全角（协议 §6.7） */
        font_draw_text(4, 2, buf, PATH_GREEN);

        snprintf(buf, sizeof(buf), "距离：%d m", (int)f->turn_dist);
        font_draw_text(4, 22, buf, PATH_GREEN);

        int km = (int)(f->total_dist / 1000);
        int frac = (int)((f->total_dist % 1000) / 100);
        snprintf(buf, sizeof(buf), "全程 %d.%d km  已完 %u%%", km, frac, (unsigned)f->progress_pct);
        font_draw_text(6, 168, buf, PATH_GREEN);

        snprintf(buf, sizeof(buf), "耗时 %u min", (unsigned)f->elapsed_min);
        font_draw_text(6, 190, buf, PATH_GREEN);

        snprintf(buf, sizeof(buf), "预计到达 %s", f->eta_time);
        font_draw_text(6, 212, buf, PATH_GREEN);

        font_draw_text(300, 168, "北", PATH_GREEN);
    }

#endif
    fb_flush();
    {
        static uint32_t n = 0;
        n++;
        if ((n % 150) == 1) {
            ESP_LOGI(TAG, "flushed frame #%lu (have=%d)", (unsigned long)n, (int)s_have);
        }
    }
}

/* 显示任务周期调用：推进虚线动画并重绘（dash_speed 来自 SET_CONFIG，默认 60 px/s） */
void render_nav_tick(float dt)
{
    if (!s_have) return;
    const espnav_config_t *cfg = config_get();
    s_anim += (float)cfg->dash_speed * dt;
    if (s_anim > 100000.0f) s_anim = 0.0f;
    draw_frame(&s_cur, s_anim);
}

/* 收帧即渲染（双保险：不依赖显示任务定时器） */
void render_nav_draw_now(void)
{
    if (!s_have) return;
    draw_frame(&s_cur, s_anim);
}

/* 立即渲染（调试/单帧） */
void render_nav_frame(const nav_frame_t *f)
{
    render_nav_set_frame(f);
    draw_frame(f, 0.0f);
}
