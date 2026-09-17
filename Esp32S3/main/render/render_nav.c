#include "render_nav.h"
#include "lcd_fb.h"
#include "geo.h"      /* geo_* / gpt_t：M4 模板几何（此前缺失导致编译失败） */
#include <math.h>
#include "esp_log.h"
#include "config.h"
#include "wifi_sta.h"      /* 屏幕显示网络状态（AP / STA 已连接） */
#include "font.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "render_nav";

/* ---- 模块级状态（统一在文件顶部定义，任何函数体内的使用都不会“先于声明”）---- */
static nav_frame_t s_cur;            /* 当前导航帧 */
static bool        s_have = false;   /* 是否已有可渲染帧 */
static bool        s_blank_req = false;
static bool  s_hold_blank = false;   /* CLEAR_SCREEN 后保持黑屏，直到收到新导航帧（否则会被占位版式立刻覆盖） */  /* CLEAR_SCREEN 清屏请求（显示任务消费） */
static bool        s_link_lost = false;  /* 链路断开：保留画面 + 中部“信号中断”提示 */
static float       s_anim = 0.0f;    /* 虚线相位 */


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

void render_nav_clear(void)
{
    s_blank_req = true;      /* CLEAR_SCREEN：由显示任务执行清屏 */
    s_hold_blank = true;
}

void render_nav_link_lost(void)
{
    s_link_lost = true;      /* 断线：保留最后画面，仅叠加提示条 */
}

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


static uint32_t s_frame_count = 0;      /* 收到的有效帧计数（显示任务据此判断数据是否真正在流动） */

uint32_t render_nav_frame_count(void)
{
    return s_frame_count;
}

void render_nav_set_frame(const nav_frame_t *f)
{
    if (!f || !f->valid) return;
    s_cur = *f;
    s_frame_count++;
    s_hold_blank = false;
    s_have = true;
    s_link_lost = false;     /* 收到新帧 -> 恢复实时画面 */
}

/* 罗盘（顶部右侧）：8 方位标签随 heading 平移 + 中央红色车头箭头（车头朝上） */
/* ---------------- 长文本水平滚动（30px/s：30fps 下每帧 1px；滚完停顿 0.6s ≈ 18 帧）---------------- */
#define SCROLL_SLOTS        3
#define SCROLL_PAUSE_FRAMES 18

static float s_scroll[SCROLL_SLOTS];
static int   s_scroll_pause[SCROLL_SLOTS];

static int scroll_off(int slot, int text_w, int avail_w)
{
    if (slot < 0 || slot >= SCROLL_SLOTS) return 0;
    if (text_w <= avail_w) {                 /* 放得下就不动 */
        s_scroll[slot] = 0.0f;
        s_scroll_pause[slot] = 0;
        return 0;
    }
    if (s_scroll_pause[slot] > 0) {          /* 停顿中 */
        s_scroll_pause[slot]--;
        return (int)s_scroll[slot];
    }
    s_scroll[slot] += 1.0f;
    if (s_scroll[slot] > (float)(text_w + 24)) {   /* 完全滚出 + 间隔后回到起点并停顿 */
        s_scroll[slot] = 0.0f;
        s_scroll_pause[slot] = SCROLL_PAUSE_FRAMES;
    }
    return (int)s_scroll[slot];
}

/* 长文本行：超出可用宽度则滚动；始终裁剪到 [x, clip1] */
static void draw_line_scroll(int slot, int x, int y, const char *text, uint16_t color, int clip1)
{
    int w = font_text_width(text);
    int off = scroll_off(slot, w, clip1 - x);
    font_draw_text_clip(x - off, y, text, color, x, clip1);
}

/* 链路断开提示条（协议 §6.7：主视图中部，黑底红字），保留画面不清屏 */
static void draw_link_lost_banner(void)
{
    const int y = 56;
    const int h = 26;
    static const char *txt = "信号中断";
    int w = font_text_width(txt);
    fb_fill_rect(0, y, FB_W - 1, y + h - 1, RGB565_BLACK);
    font_draw_text((FB_W - w) / 2, y + 5, txt, RGB565_RED);
}

/* 顶部网络状态行：STA 已连接 <ip> / STA 未连接 AP:ESPNav-AP
 * 用途：不用看串口也能确认 ESP32 当前是"只开热点"还是"已连上手机热点"。 */
static void draw_net_status(void)
{
    static char buf[40];
    if (wifi_sta_is_connected()) {
        const char *ip = wifi_sta_ip_str();
        snprintf(buf, sizeof(buf), "STA %s", (ip && ip[0]) ? ip : "OK");
    } else {
        snprintf(buf, sizeof(buf), "STA 未连接 AP:ESPNav-AP");
    }
    font_draw_text(6, 218, buf, 0x7BEF);       /* 底部最后一行（浅灰） */
}

/* 罗盘带（顶部居中，中心 x=160 与道路中轴对齐）
 * · 刻度统一 8px（15°/45° 同长）；主方位（北/东/南/西）位置不画线，改写中文
 * · 罗盘内不再画箭头/中轴线（画面上只保留路面上那一个黄色车头）
 * · “方位 + 角度”显示在车头正下方：透明底 + 亮青字 */
static void draw_compass(const nav_frame_t *f)
{
    static const char *main_labels[4] = { "北", "东", "南", "西" };
    static const char *dirs8[8] = { "北", "东北", "东", "东南", "南", "西南", "西", "西北" };
    const int bx = 160;
    const int spread = 150;                     /* ±150px 映射 ±180° */
    const float px_per_deg = (float)spread / 180.0f;
    int heading = ((f->heading % 360) + 360) % 360;

    /* 1) 刻度：统一 8px（主方位位置留给中文，不画线） */
    for (int a = 0; a < 360; a += 15) {
        if (a % 90 == 0) continue;
        int d = ((a - heading + 540) % 360) - 180;
        int x = bx + (int)((float)d * px_per_deg);
        if (x < bx - spread || x > bx + spread) continue;
        fb_line(x, 4, x, 12, (a % 45 == 0) ? PATH_GREEN : 0x7BEF);
    }

    /* 2) 主方位中文（替代长刻度线） */
    for (int i = 0; i < 4; i++) {
        int d = ((i * 90 - heading + 540) % 360) - 180;
        int x = bx + (int)((float)d * px_per_deg);
        if (x < bx - spread + 8 || x > bx + spread - 8) continue;
        font_draw_text(x - font_text_width(main_labels[i]) / 2, 4, main_labels[i], PATH_GREEN);
    }

    /* 3) 方位 + 角度：车头正下方，透明底 + 亮青字 */
    {
        char buf[24];
        int idx = ((heading + 22) / 45) % 8;
        int w;
        snprintf(buf, sizeof(buf), "%s %d°", dirs8[idx], heading);
        w = font_text_width(buf);
        font_draw_text(bx - w / 2, 126, buf, RGB565_CYAN);
    }
}

/* 行程图（右下 overview）：网格 + 路径 + 当前位置点
 * 协议 §3.1 第 5 条：坐标为相对小地图左上角像素坐标；渲染端内边距 8、y 翻转(40-y)。 */
/* 行程图专用小号“北”字（8px 点阵；罗盘主方位仍用 16px 字库） */
static const uint8_t NORTH8[8] = { 0x08, 0x08, 0x6C, 0x08, 0x08, 0x69, 0x06, 0x00 };

static void draw_north8(int x, int y)
{
    for (int r = 0; r < 8; r++) {
        uint8_t bits = NORTH8[r];
        for (int c = 0; c < 8; c++) {
            if (bits & (0x80 >> c)) fb_pixel(x + c, y + r, PATH_GREEN);
        }
    }
}

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
    /* 起终点标记：轨迹首点=起点(绿)、末点=终点(红)；当前位置仍为黄点（App 实时更新） */
    if (f->overview_n >= 2) {
        int sx = ax + 8 + f->overview[0].x;
        int sy = ay + 8 + (40 - f->overview[0].y);
        int tx = ax + 8 + f->overview[f->overview_n - 1].x;
        int ty = ay + 8 + (40 - f->overview[f->overview_n - 1].y);
        fb_fill_rect(sx - 2, sy - 2, sx + 2, sy + 2, RGB565_GREEN);
        fb_fill_rect(tx - 2, ty - 2, tx + 2, ty + 2, RGB565_RED);
    }
    if (f->overview_dot_valid) {
        int dx = ax + 8 + f->overview_dot.x, dy = ay + 8 + (40 - f->overview_dot.y);
        fb_fill_rect(dx - 2, dy - 2, dx + 2, dy + 2, RGB565_YELLOW);
    }
    draw_north8(ax + 80, ay + 2);                        /* 右上角小号“北”（8px 点阵） */
    {   /* 十字线：位于“北”字下方；竖线上端带向上小箭头指向北 */
        const int vx = ax + 84;                     /* 8px 北字中心 = 80 + 4 */
        const int yTop = ay + 13;                   /* 北字(ay+2..ay+9) 下方 3px 起 */
        const int ny = ay + 26;                     /* 十字中心（一个网格处） */
        fb_line(vx, yTop + 4, vx, ny + 9, PATH_GREEN);             /* 竖线（上端留箭头位） */
        fb_line(vx - 3, yTop + 5, vx, yTop, PATH_GREEN);           /* 箭头左斜 */
        fb_line(vx + 3, yTop + 5, vx, yTop, PATH_GREEN);           /* 箭头右斜 */
        fb_line(vx - 9, ny, vx + 9, ny, PATH_GREEN);               /* 横线 */
    }
}

/* ================= M4：路况模板渲染（参照 nav_sim_v2.html readRoad / 几何规格 V0.3） ================= */
#define TPL_SIDE   35.0f
#define EDGE_LEFT  20
#define EDGE_RIGHT 300
#define EDGE_TOP   26
#define EDGE_BOT  134
#define ROAD_GRAY2 0x73AE

static int turn_vertex(const npt_t *p, int n)
{
    if (n < 3) return -1;
    int bi = -1; float ba = 0.0f;
    for (int i = 1; i < n - 1; i++) {
        float ux = (float)(p[i].x - p[i-1].x), uy = (float)(p[i].y - p[i-1].y);
        float vx = (float)(p[i+1].x - p[i].x), vy = (float)(p[i+1].y - p[i].y);
        float du = sqrtf(ux*ux + uy*uy), dv = sqrtf(vx*vx + vy*vy);
        if (du < 1.0f || dv < 1.0f) continue;
        float dot = (ux*vx + uy*vy) / (du*dv);
        if (dot > 1.0f) dot = 1.0f;
        if (dot < -1.0f) dot = -1.0f;
        float ang = acosf(dot);
        if (ang > ba) { ba = ang; bi = i; }
    }
    return (ba > 0.35f) ? bi : -1;
}

/* 沿中心线（平面坐标）填路面：fatten(含投影) -> 多边形填充 */
/* 点是否在屏内：App 投影异常时会给出越界坐标，照画会出现"随机折线/图案漂移"，故一律跳过 */
static bool pt_in_screen(npt_t p)
{
    return p.x >= 0 && p.x < FB_W && p.y >= 0 && p.y < FB_H;
}

static void road_fill(const npt_t *pts, int n, float half, uint16_t color)
{
    if (n < 2) return;
    gpt_t ip[NAV_MAX_PTS], l[NAV_MAX_PTS], r[NAV_MAX_PTS];
    for (int i = 0; i < n; i++) { ip[i].x = (float)pts[i].x; ip[i].y = (float)pts[i].y; }
    int on = 0;
    geo_fatten(ip, n, half, l, r, &on);
    if (on < 2) return;
    int xs[128], ys[128];   /* geo_fatten 输出 <= 2*16+2 点 -> 多边形 <= 68 点 */
    int k = 0;
    for (int i = 0; i < on; i++)    { xs[k] = (int)l[i].x; ys[k] = (int)l[i].y; k++; }
    for (int i = on - 1; i >= 0; i--) { xs[k] = (int)r[i].x; ys[k] = (int)r[i].y; k++; }
    fb_fill_poly(xs, ys, k, color);
}

/* 边线：flow=true 流动虚线（相位 anim），否则静态实线 */
static void road_edges(const npt_t *pts, int n, float half, bool flow, float anim)
{
    if (n < 2) return;
    gpt_t ip[NAV_MAX_PTS], l[NAV_MAX_PTS], r[NAV_MAX_PTS];
    for (int i = 0; i < n; i++) { ip[i].x = (float)pts[i].x; ip[i].y = (float)pts[i].y; }
    int on = 0;
    geo_fatten(ip, n, half, l, r, &on);
    for (int i = 0; i + 1 < on; i++) {
        if (flow) {
            fb_dashed_line_off((int)l[i].x, (int)l[i].y, (int)l[i+1].x, (int)l[i+1].y, RGB565_WHITE, 8, 6, anim);
            fb_dashed_line_off((int)r[i].x, (int)r[i].y, (int)r[i+1].x, (int)r[i+1].y, RGB565_WHITE, 8, 6, anim);
        } else {
            fb_line((int)l[i].x, (int)l[i].y, (int)l[i+1].x, (int)l[i+1].y, 0xBBBB);
            fb_line((int)r[i].x, (int)r[i].y, (int)r[i+1].x, (int)r[i+1].y, 0xBBBB);
        }
    }
}

/* 绿路径（投影后双层描边） */
static void road_path(const npt_t *pts, int n)
{
    if (n < 2) return;
    gpt_t pr[NAV_MAX_PTS];
    for (int i = 0; i < n; i++) { gpt_t g = { (float)pts[i].x, (float)pts[i].y }; pr[i] = geo_proj_pt(g); }
    /* 【临时探针】模板点首末坐标（用户报告"绿色折线漂移、进某路段后消失"）：
     * 与 past_center 的探针配合，用来确定到底是哪个数组给出了越界坐标。 */
    {
        static uint32_t rp_log_n = 0;
        if (n > 0 && (rp_log_n++ % 30u) == 0u) {
            ESP_LOGW(TAG, "road pts n=%d 首=(%d,%d) 末=(%d,%d)",
                     n, pts[0].x, pts[0].y, pts[n - 1].x, pts[n - 1].y);
        }
    }
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i + 1 < n; i++) {
            /* 越界点直接跳过（宁可少画一段，也不把线画到文字/行程图区域去） */
            if (!pt_in_screen(pts[i]) || !pt_in_screen(pts[i + 1])) continue;
            int x0 = (int)pr[i].x, y0 = (int)pr[i].y, x1 = (int)pr[i+1].x, y1 = (int)pr[i+1].y;
            if (pass == 0) { fb_line(x0, y0, x1, y1, 0x0320); fb_line(x0, y0+1, x1, y1+1, 0x0320); }
            else           { fb_line(x0, y0, x1, y1, PATH_GREEN); fb_line(x0+1, y0, x1+1, y1, PATH_GREEN); }
        }
    }
}

/* 方位向量（屏幕系，y 向下） */
static void dir_vec(const char *name, float *vx, float *vy)
{
    float v0 = 0.0f, v1 = -1.0f;
    if (!strcmp(name, "E"))       { v0 = 1.0f;  v1 = 0.0f; }
    else if (!strcmp(name, "W"))  { v0 = -1.0f; v1 = 0.0f; }
    else if (!strcmp(name, "N"))  { v0 = 0.0f;  v1 = -1.0f; }
    else if (!strcmp(name, "S"))  { v0 = 0.0f;  v1 = 1.0f; }
    else if (!strcmp(name, "NE")) { v0 = 0.707f;  v1 = -0.707f; }
    else if (!strcmp(name, "NW")) { v0 = -0.707f; v1 = -0.707f; }
    else if (!strcmp(name, "SE")) { v0 = 0.707f;  v1 = 0.707f; }
    else if (!strcmp(name, "SW")) { v0 = -0.707f; v1 = 0.707f; }
    *vx = v0; *vy = v1;
}

/* 沿方位延伸到屏幕边界（平面坐标） */
static void road_to_edge(npt_t from, const char *dir, npt_t *out)
{
    float vx, vy;
    dir_vec(dir, &vx, &vy);
    float t = 1e9f;
    if (vx > 0) t = fminf(t, ((float)EDGE_RIGHT - from.x) / vx);
    if (vx < 0) t = fminf(t, ((float)EDGE_LEFT  - from.x) / vx);
    if (vy > 0) t = fminf(t, ((float)EDGE_BOT   - from.y) / vy);
    if (vy < 0) t = fminf(t, ((float)EDGE_TOP   - from.y) / vy);
    if (t < 8.0f) t = 8.0f;
    if (t > 600.0f) t = 600.0f;
    out->x = (int16_t)(from.x + vx * t);
    out->y = (int16_t)(from.y + vy * t);
}

/* 单条支路（到边）：fill + 边线 */
static void arm_draw(npt_t from, const char *dir, float half, bool flow, float anim)
{
    npt_t seg[2];
    seg[0] = from;
    road_to_edge(from, dir, &seg[1]);
    road_fill(seg, 2, half, ROAD_GRAY2);
    road_edges(seg, 2, half, flow, anim);
}

static void draw_road(const nav_frame_t *f, float anim)
{
    const nav_road_t *rd = &f->road;
    const npt_t *p = rd->pts;
    int n = rd->pts_n;
    float half = (rd->half > 0) ? (float)rd->half : 62.0f;
    const char *type = rd->type;
    int k;
    npt_t C;
    npt_t inS[NAV_MAX_PTS];
    int inN;

    if (!strcmp(type, "straight") || !strcmp(type, "curve")) {
        if (n >= 2) { road_fill(p, n, half, ROAD_GRAY2); road_edges(p, n, half, true, anim); road_path(p, n); }
        return;
    }
    if (!strcmp(type, "tjunc")) {
        k = turn_vertex(p, n);
        if (k <= 0) {
            if (n >= 2) { road_fill(p, n, half, ROAD_GRAY2); road_edges(p, n, half, true, anim); road_path(p, n); }
            return;
        }
        road_fill(p, k + 1, half, ROAD_GRAY2);        road_edges(p, k + 1, half, true, anim);
        road_fill(p + k, n - k, TPL_SIDE, ROAD_GRAY2); road_edges(p + k, n - k, TPL_SIDE, true, anim);
        road_path(p, n);
        return;
    }
    if (!strcmp(type, "cross")) {
        const char *toDir;
        k = turn_vertex(p, n);
        C = (k > 0) ? p[k] : p[n / 2];
        inN = 0;
        if (k > 0) { for (int i = 0; i <= k; i++) inS[inN++] = p[i]; }
        else { inS[inN++] = p[0]; inS[inN++] = C; }
        road_fill(inS, inN, half, ROAD_GRAY2);
        road_edges(inS, inN, half, true, anim);
        toDir = !strcmp(rd->dir, "left") ? "W" : (!strcmp(rd->dir, "right") ? "E" : "N");
        arm_draw(C, "N", TPL_SIDE, !strcmp("N", toDir), anim);
        arm_draw(C, "W", TPL_SIDE, !strcmp("W", toDir), anim);
        arm_draw(C, "E", TPL_SIDE, !strcmp("E", toDir), anim);
        road_path(p, n);
        return;
    }
    if (!strcmp(type, "fork")) {
        npt_t mainLine[2];
        mainLine[0].x = (int16_t)(n ? p[0].x : 160);
        mainLine[0].y = EDGE_BOT;
        mainLine[1] = mainLine[0];
        mainLine[1].y = EDGE_TOP;
        road_fill(mainLine, 2, half, ROAD_GRAY2);
        road_edges(mainLine, 2, half, false, 0.0f);
        if (n >= 2) {
            road_fill(p, n, TPL_SIDE - 6.0f, ROAD_GRAY2);
            road_edges(p, n, TPL_SIDE - 6.0f, true, anim);
            road_path(p, n);
        }
        return;
    }
    if (!strcmp(type, "multi")) {
        struct { const char *d; int16_t x, y; } arms[5];
        float tx, ty, tl;
        const char *best = NULL;
        float bd = 0.85f;
        k = turn_vertex(p, n);
        C = (k > 0) ? p[k] : p[n / 2];
        inN = 0;
        if (k > 0) { for (int i = 0; i <= k; i++) inS[inN++] = p[i]; }
        else { inS[inN++] = p[0]; inS[inN++] = C; }
        road_fill(inS, inN, half, ROAD_GRAY2);
        road_edges(inS, inN, half, true, anim);
        arms[0].d = "N";  arms[0].x = C.x;         arms[0].y = EDGE_TOP;
        arms[1].d = "W";  arms[1].x = EDGE_LEFT;   arms[1].y = C.y;
        arms[2].d = "E";  arms[2].x = EDGE_RIGHT;  arms[2].y = C.y;
        arms[3].d = "NW"; arms[3].x = 110;         arms[3].y = EDGE_TOP;
        arms[4].d = "SE"; arms[4].x = 236;         arms[4].y = 100;
        tx = (n ? (float)(p[n-1].x - C.x) : 0.0f);
        ty = (n ? (float)(p[n-1].y - C.y) : 0.0f);
        tl = sqrtf(tx*tx + ty*ty);
        if (tl > 1.0f) {
            for (int i = 0; i < 5; i++) {
                float vx, vy, dot;
                dir_vec(arms[i].d, &vx, &vy);
                dot = (tx*vx + ty*vy) / tl;
                if (dot > bd) { bd = dot; best = arms[i].d; }
            }
        }
        for (int i = 0; i < 5; i++) {
            npt_t seg[2];
            bool flow;
            seg[0] = C;
            seg[1].x = arms[i].x;
            seg[1].y = arms[i].y;
            flow = (best && !strcmp(arms[i].d, best));
            road_fill(seg, 2, TPL_SIDE, ROAD_GRAY2);
            road_edges(seg, 2, TPL_SIDE, flow, anim);
        }
        road_path(p, n);
        return;
    }
    if (!strcmp(type, "roundabout")) {
        float b, aIn, bIn, rw, sIn;
        int cx = rd->cx, cy = rd->cy;
        const char *target = NULL;
        int ti;
        npt_t lead[2];
        geo_rbt_metrics((float)rd->r, &b, &aIn, &bIn, &rw, &sIn);
        lead[0].x = 160; lead[0].y = 144;
        lead[1].x = (int16_t)cx; lead[1].y = (int16_t)(cy + b);
        road_fill(lead, 2, sIn, ROAD_GRAY2);
        road_edges(lead, 2, sIn, true, anim);
        fb_ellipse(cx, cy, rd->r, (int)b, ROAD_GRAY2);
        fb_ellipse(cx, cy, (int)aIn, (int)bIn, RGB565_BLACK);
        ti = 1;
        if (!strncmp(rd->dir, "exit", 4)) ti = atoi(rd->dir + 4);
        if (ti < 1) ti = 1;
        if (rd->exits_n > 0) {
            if (ti > rd->exits_n) ti = rd->exits_n;
            target = rd->exits[ti - 1];
        }
        for (int i = 0; i < rd->exits_n; i++) {
            float th = geo_azimuth_rad(rd->exits[i]);
            npt_t ex, seg[2];
            bool flow;
            ex.x = (int16_t)(cx + (float)rd->r * cosf(th));
            ex.y = (int16_t)(cy + b * sinf(th));
            seg[0] = ex;
            road_to_edge(ex, rd->exits[i], &seg[1]);
            flow = (target && !strcmp(rd->exits[i], target));
            road_fill(seg, 2, rw, ROAD_GRAY2);
            road_edges(seg, 2, rw, flow, anim);
        }
        if (target) {
            float thS = geo_azimuth_rad("S");
            float thT = geo_azimuth_rad(target);
            float sweep, aM, bM;
            int spin = 1;
            npt_t path[NAV_MAX_PTS];
            int pn = 0;
            if (rd->exits_n > 0) {
                float d0 = geo_azimuth_rad(rd->exits[0]) - thS;
                while (d0 >  3.1415927f) d0 -= 6.2831853f;
                while (d0 < -3.1415927f) d0 += 6.2831853f;
                spin = (d0 >= 0) ? 1 : -1;
            }
            sweep = geo_sweep_directed(thS, thT, spin);
            path[pn].x = 160; path[pn].y = 144; pn++;
            path[pn].x = (int16_t)cx; path[pn].y = (int16_t)(cy + b); pn++;
            aM = ((float)rd->r + aIn) / 2.0f;
            bM = (b + bIn) / 2.0f;
            for (int i = 1; i <= 8 && pn < NAV_MAX_PTS; i++) {
                float th = thS + sweep * (float)i / 8.0f;
                path[pn].x = (int16_t)(cx + aM * cosf(th));
                path[pn].y = (int16_t)(cy + bM * sinf(th));
                pn++;
            }
            {
                npt_t exT, outT;
                exT.x = (int16_t)(cx + (float)rd->r * cosf(thT));
                exT.y = (int16_t)(cy + b * sinf(thT));
                road_to_edge(exT, target, &outT);
                if (pn < NAV_MAX_PTS) { path[pn] = outT; pn++; }
            }
            road_path(path, pn);
        }
        return;
    }
    if (n >= 2) {
        road_fill(p, n, half, ROAD_GRAY2);
        road_edges(p, n, half, true, anim);
        road_path(p, n);
    }
}

static void draw_frame(const nav_frame_t *f, float anim)
{
    if (!f || !f->valid) return;
    fb_clear(RGB565_BLACK);
    fb_line(0, 160, FB_W - 1, 160, RGB565_DGRAY);
    fb_line(220, 160, 220, FB_H - 1, RGB565_DGRAY);

    if (f->road.present) {
        draw_road(f, anim);                        /* M4：路况模板（road 字段驱动） */
    } else if (f->center_n >= 2) {
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
    /* ---- 临时排查（用户线索：漂移绿色折线只在"车头下方"）----
     * past_center = App 发来的"已走过路径"，屏幕上位于车头【下方】，与该现象吻合。
     * ① 打印其真实坐标（每 30 帧一次，避免刷屏）② 暂时屏蔽绘制，验证折线是否随之消失。
     * 验证完成后：删除 #if 0 / #endif 即可恢复绘制。 */
    if (f->past_n > 0) {
        static uint32_t past_log_n = 0;
        if ((past_log_n++ % 30u) == 0u) {
            ESP_LOGW(TAG, "past_center n=%d 首=(%d,%d) 末=(%d,%d)",
                     f->past_n, f->past_center[0].x, f->past_center[0].y,
                     f->past_center[f->past_n - 1].x, f->past_center[f->past_n - 1].y);
        }
    }
#if 0   /* 【临时屏蔽】验证"车头下方绿色折线漂移"是否由 past_center 绘制产生 */
    for (int i = 0; i + 1 < f->past_n; i++) {
        if (!pt_in_screen(f->past_center[i]) || !pt_in_screen(f->past_center[i + 1])) continue;
        fb_line(f->past_center[i].x, f->past_center[i].y, f->past_center[i + 1].x, f->past_center[i + 1].y, PATH_GREEN);
    }
#endif
    for (int i = 0; i + 1 < f->route_n; i++) {
        if (!pt_in_screen(f->route_center[i]) || !pt_in_screen(f->route_center[i + 1])) continue;
        fb_line(f->route_center[i].x, f->route_center[i].y, f->route_center[i + 1].x, f->route_center[i + 1].y, PATH_GREEN);
    }
/* 车辆光标 */
    /* 路线中轴线（绿色，贯通道路远端 NAV_FAR_Y 到近端 NAV_NEAR_Y；随后绘制车头 -> 车头压线） */
    fb_line(NAV_CX, NAV_FAR_Y, NAV_CX, NAV_NEAR_Y, RGB565_GREEN);
    if (f->pos_valid) fb_triangle(f->pos.x, f->pos.y, 14, RGB565_YELLOW);

#if RENDER_TEXT
    /* 罗盘 + 行程图 + 底部网络状态（不依赖 RENDER_TEXT 开关） */
    draw_compass(f);
    draw_net_status();
    if (f->road_name[0]) {
        static char rb[64];
        font_clip_utf8(f->road_name, 12 * 16, rb, sizeof(rb));  /* 放宽，超出由滚动显示 */
        draw_line_scroll(0, 4, 24, rb, PATH_GREEN, 312);        /* 罗盘下方，恢复全宽 */
    }
    draw_overview(f);

    /* ---- 文字层（hint / 距离 / 统计）---- */
    {
        static char buf[64];                 /* static：避免显示任务栈压力 */
        font_clip_utf8(f->hint, 12 * 16, buf, sizeof(buf));     /* 放宽，超出由滚动显示 */
        draw_line_scroll(1, 4, 44, buf, PATH_GREEN, 312);

        snprintf(buf, sizeof(buf), "距离：%d m", (int)f->turn_dist);
        font_draw_text(4, 64, buf, PATH_GREEN);

        if (f->notice[0]) {                                     /* 路况/设施提示（红绿灯倒计时若将来可得也放这里） */
            static char nb[40];
            font_clip_utf8(f->notice, 20 * 16, nb, sizeof(nb));
            draw_line_scroll(2, 4, 84, nb, RGB565_YELLOW, 316);
        }

        int km = (int)(f->total_dist / 1000);
        int frac = (int)((f->total_dist % 1000) / 100);
        snprintf(buf, sizeof(buf), "全程 %d.%dkm 已完%u%%", km, frac, (unsigned)f->progress_pct);
        font_draw_text(6, 164, buf, PATH_GREEN);

        snprintf(buf, sizeof(buf), "耗时 %u min", (unsigned)f->elapsed_min);
        font_draw_text(6, 182, buf, PATH_GREEN);

        snprintf(buf, sizeof(buf), "预计到达 %s", f->eta_time);
        font_draw_text(6, 200, buf, PATH_GREEN);

        /* 北向标记统一由 draw_overview() 绘制（固定表示行程图方向），此处不再重复 */
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
    if (s_blank_req) {                       /* 黑屏待机：只在本任务清屏（SPI 仅显示任务访问） */
        s_blank_req = false;
        s_have = false;
        fb_clear(RGB565_BLACK);
        fb_flush();
        ESP_LOGI(TAG, "screen cleared -> blank standby");
        return;
    }
    if (s_hold_blank) return;                /* CLEAR_SCREEN 后保持黑屏，直到收到新导航帧 */
    /* 已连接但还没有导航数据：画“导航版式 + 中央提示”，而不是停在开机画面 */
    if (!s_have) {
        nav_frame_t empty;
        const char *tip = "请在App开始导航";
        int w;
        memset(&empty, 0, sizeof(empty));
        empty.valid = true;                  /* 用空帧驱动版式（路面/罗盘/行程图都在） */
        empty.heading = 0;
        empty.pos.x = NAV_CX; empty.pos.y = 110; empty.pos_valid = true;
        s_anim += dt * 40.0f;                /* 虚线仍流动 */
        draw_frame(&empty, s_anim);
        w = font_text_width(tip);
        font_draw_text((FB_W - w) / 2, 150, tip, RGB565_CYAN);
        fb_flush();
        return;
    }
    const espnav_config_t *cfg = config_get();
    if (cfg->anim_enable) {                  /* anim_enable=false => 静态虚线（不推进相位） */
        s_anim += (float)cfg->dash_speed * dt;
        if (s_anim > 100000.0f) s_anim = 0.0f;
    }
    draw_frame(&s_cur, s_anim);
    if (s_link_lost) draw_link_lost_banner();
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
/* ---------------- 开机画面（B 科技版）----------------
 * 暗网格 + 四角标记 + 标题/副标题 + 扫描线 + 状态行；全部使用 16px/8px 现有字库 */
void render_nav_boot(int stage, const char *sub)
{
    static float anim = 0.0f;
    static const char *st_text[4] = { "系统启动中", "正在连接 WiFi", "等待手机App连接",
                                      "已连接 - 等待导航数据" };
    const uint16_t GRID = 0x10C2;                 /* 极暗绿灰 */
    int i, w, y;
    char line[48];

    anim += 1.5f;
    if (anim > 1000.0f) anim = 0.0f;
    fb_clear(RGB565_BLACK);

    /* 暗网格背景（每 16px） */
    for (i = 0; i < FB_W; i += 16) fb_line(i, 0, i, FB_H - 1, GRID);
    for (y = 0; y < FB_H; y += 16) fb_line(0, y, FB_W - 1, y, GRID);

    /* 四角 L 形标记 */
    fb_line(8, 8, 22, 8, PATH_GREEN);      fb_line(8, 8, 8, 22, PATH_GREEN);
    fb_line(FB_W - 9, 8, FB_W - 23, 8, PATH_GREEN);    fb_line(FB_W - 9, 8, FB_W - 9, 22, PATH_GREEN);
    fb_line(8, FB_H - 9, 22, FB_H - 9, PATH_GREEN);    fb_line(8, FB_H - 9, 8, FB_H - 23, PATH_GREEN);
    fb_line(FB_W - 9, FB_H - 9, FB_W - 23, FB_H - 9, PATH_GREEN);
    fb_line(FB_W - 9, FB_H - 9, FB_W - 9, FB_H - 23, PATH_GREEN);

    /* 标题（3 倍放大并上移，避免与网格/副标题叠压）+ 副标题 */
    w = font_text_width("EspNav v1.0") * 3;
    font_draw_text_scaled((FB_W - w) / 2, 40, "EspNav v1.0", PATH_GREEN, 3);
    w = font_text_width("可穿戴导航屏·骑行版");
    font_draw_text((FB_W - w) / 2, 92, "可穿戴导航屏·骑行版", 0x7BEF);

    /* 扫描线（动态） */
    y = 132 + (int)(24.0f * (anim - (int)(anim / 56.0f) * 56.0f) / 56.0f * 2.0f);
    if (y > 156) y = 156 - (y - 156);
    fb_line(80, y, 240, y, PATH_GREEN);

    /* 状态行 */
    if (stage < 0) stage = 0;
    if (stage > 3) stage = 3;
    snprintf(line, sizeof(line), "%s", st_text[stage]);
    w = font_text_width(line);
    font_draw_text((FB_W - w) / 2, 182, line, RGB565_WHITE);
    {
        const char *line2 = sub;                                     /* 副行由调用方给出 */
        if (line2 && line2[0]) {
            w = font_text_width(line2);
            font_draw_text((FB_W - w) / 2, 206, line2, 0x7BEF);
        }
    }
    fb_flush();          /* 关键：fb_* 只写内存帧缓冲，必须 flush 才推送到 LCD */
}
