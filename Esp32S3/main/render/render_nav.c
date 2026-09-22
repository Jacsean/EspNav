#include "render_nav.h"
#include "lcd_fb.h"
#include "geo.h"      /* geo_* / gpt_t：M4 模板几何（此前缺失导致编译失败） */
#include <math.h>
#include "esp_log.h"
#include "config.h"
#include "map_image.h"    /* 【M3.1】地图底图（App 截图通道） */
#include "battery.h"       /* 【M7】电量显示（BAT_ADC = GPIO9） */
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
static char        s_clock[NAV_CLOCK_MAX + 1];   /* 【M2.6】CLOCK 报文缓存：待机画面也显示时间 */


/* 临时二分开关：1=渲染文字层；0=跳过文字（用于定位黑屏/崩溃是否由文字渲染引起） */
#define RENDER_TEXT 1   /* 文字渲染已恢复（黑屏根因=CS 控制，与文字无关） */

/* 与 nav_sim_v2.html 冻结基准一致：主视图 320x160 / 近宽150 远宽36 / y近130 远28 */
#define NAV_CX        160
#define NAV_NEAR_Y    144     /* 路面下边沿下移一个车身（2026-09-14 反馈） */
#define NAV_FAR_Y     28
#define NAV_NEAR_HALF 75      /* 近宽 150 */
#define NAV_FAR_HALF  18      /* 远宽 36  */
/** 【M2.4】颜色改为"可配置"：draw_frame 每帧从 config 同步到下面这几个文件级变量，
 *  于是全文所有 PATH_GREEN / ROAD_GRAY 用法自动跟随 App 设置（0 = 用默认色兜底）。
 *  对应 App 设置项：主色 / 轨迹色 / 网格色 / 路面色 / 车头色 / 提示色。 */
#define DEF_COL_MAIN   0x07E0   /* 绿：文字/罗盘/路名/统计/时间/fallback 中心线 */
#define DEF_COL_ROAD   0x73AE   /* 灰：路面 */
#define DEF_COL_TRACK  0x5D9F   /* 亮蓝：行程图轨迹（用户要求蓝色系） */
#define DEF_COL_GRID   0x8450   /* 灰绿：行程图网格基准色（再乘 grid_bright） */
#define DEF_COL_CAR    0xFFE0   /* 黄：车头三角 */
#define DEF_COL_HINT   0xFFE0   /* 黄：提示/警示行 */

static uint16_t s_col_main  = DEF_COL_MAIN;
static uint16_t s_col_road  = DEF_COL_ROAD;
static uint16_t s_col_track = DEF_COL_TRACK;
static uint16_t s_col_grid0 = DEF_COL_GRID;   /* 网格基准色（亮度再乘 grid_bright） */
static uint16_t s_col_car   = DEF_COL_CAR;
static uint16_t s_col_hint  = DEF_COL_HINT;

#define ROAD_GRAY     s_col_road  /* 路面灰（配置） */
#define PATH_GREEN    s_col_main  /* 主文字/罗盘/中心线颜色（配置） */

/* ---- 绘制来源颜色分离（用户建议：每个元素用不同颜色，折线漂移时一眼看出是谁画的）----
 * 排查完成后可以把它们改回绿色，但保留分离对后续定位更有利。 */
#define DBG_ROUTE     0xF800   /* App 未走路径      ：红 */
#define DBG_PAST      0x001F   /* App 已走路径      ：蓝 */
#define DBG_ROADPATH  0xF81F   /* 道路模板中心绿线：品红 */
#define DBG_CENTERLN  0x07FF   /* fallback 车道中线 ：青 */
#define DBG_OVERVIEW  0x780F   /* 行程图轨迹        ：紫 */

/* 【M2.6】记录 App 下发的当前时间（CLOCK 报文）；待机画面据此显示 */
void render_nav_set_clock(const char *hhmmss)
{
    if (!hhmmss) return;
    strncpy(s_clock, hhmmss, NAV_CLOCK_MAX);
    s_clock[NAV_CLOCK_MAX] = 0;
}

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

/* ---- 屏幕坐标版路面填充/边界：每个点用【自己的相邻点方向】求局部法线，半宽沿路径收窄。
 * 为什么不再用 quad_from_centerline：它只拿【首末点】算一条法线去撑开四边形，
 * 路径一弯（尤其终点前那段），整个四边形会跟着那条法线一起旋转偏移（用户实测）。
 * 输入点已是屏幕坐标，这里不再做 geo_proj 投影。 ---- */
static void loc_normal(const npt_t *p, int n, int i, float *nx, float *ny)
{
    int a = (i > 0) ? (i - 1) : i;
    int b = (i < n - 1) ? (i + 1) : i;
    float dx = (float)(p[b].x - p[a].x);
    float dy = (float)(p[b].y - p[a].y);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) { *nx = 1.0f; *ny = 0.0f; return; }
    *nx = -dy / len;
    *ny = dx / len;
}

static float half_at(int i, int n, int nearHalf, int farHalf)
{
    float t = (float)i / (float)(n - 1);
    return (float)nearHalf + ((float)farHalf - (float)nearHalf) * t;
}

static void road_fill_screen(const npt_t *pts, int n, int nearHalf, int farHalf, uint16_t color)
{
    if (n < 2) return;
    int xs[64], ys[64];
    int k = 0;
    for (int i = 0; i < n; i++) {
        float nx, ny;
        loc_normal(pts, n, i, &nx, &ny);
        float hw = half_at(i, n, nearHalf, farHalf);
        xs[k] = pts[i].x + (int)(nx * hw);
        ys[k] = pts[i].y + (int)(ny * hw);
        k++;
    }
    for (int i = n - 1; i >= 0; i--) {
        float nx, ny;
        loc_normal(pts, n, i, &nx, &ny);
        float hw = half_at(i, n, nearHalf, farHalf);
        xs[k] = pts[i].x - (int)(nx * hw);
        ys[k] = pts[i].y - (int)(ny * hw);
        k++;
    }
    fb_fill_poly(xs, ys, k, color);
}

static void road_edges_screen(const npt_t *pts, int n, int nearHalf, int farHalf, float anim)
{
    if (n < 2) return;
    for (int i = 0; i + 1 < n; i++) {
        float n0x, n0y, n1x, n1y;
        loc_normal(pts, n, i, &n0x, &n0y);
        loc_normal(pts, n, i + 1, &n1x, &n1y);
        float h0 = half_at(i, n, nearHalf, farHalf);
        float h1 = half_at(i + 1, n, nearHalf, farHalf);
        fb_dashed_line_off(pts[i].x + (int)(n0x * h0), pts[i].y + (int)(n0y * h0),
                           pts[i + 1].x + (int)(n1x * h1), pts[i + 1].y + (int)(n1y * h1),
                           RGB565_WHITE, 8, 6, anim);
        fb_dashed_line_off(pts[i].x - (int)(n0x * h0), pts[i].y - (int)(n0y * h0),
                           pts[i + 1].x - (int)(n1x * h1), pts[i + 1].y - (int)(n1y * h1),
                           RGB565_WHITE, 8, 6, anim);
    }
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

/* 行程图区域：向左侧（文字提示区方向）拓宽 3 个网格 = 30px（用户要求，原 220..320）。 */
#define OV_AX 190
#define OV_AW 130

/* 行程图局部坐标跨度：必须与 App 端 NavStateMapper.OV_SPAN 保持一致。
 * 原为 40 —— 长路线下相邻路口会被量化到同一个点，2026-09 提升到 200（分辨精度 x5）。 */
#define OV_SPAN 200
#define OV_BOX  72      /* 内容等比正方形边长 */
#define OV_OX   ((OV_AW - OV_BOX) / 2)   /* 内容在区域宽度内【居中】（用户要求：不要紧贴左边） */
#define OV_OY   4

/* 行程图局部坐标 -> 画布像素（等比、北在上） */
static void ov_to_px(const npt_t *p, int *px, int *py)
{
    const int ax = OV_AX, ay = 160;       /* 行程图区起点（向左拓宽后 190） */
    *px = ax + OV_OX + (int)p->x * OV_BOX / OV_SPAN;
    *py = ay + OV_OY + (OV_SPAN - (int)p->y) * OV_BOX / OV_SPAN;
}

/* 【M1.3】行程图网格色：按 grid_bright(0-100) 从暗绿灰插值到亮绿灰（RGB565）
 *   原固定 0x2104 ≈ #202020 在暗底衬上几乎看不见（用户要求"网格更醒目"）。
 *   0% -> 0x2104(≈#202020)、100% -> 0xD6DA(≈#D4DAD4)；默认 55% ≈ #808680。
 *   boost：主格线（每 50px）再亮一档（+25），形成结构感。 */
static uint16_t grid_color(int bright, int boost)
{
    int v = bright + boost;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    /* 【M2.4】以配置的"网格色"（s_col_grid0）为基准按亮度比例缩放 ——
     * App 里改网格色直接生效，"网格亮度"仍可微调对比。 */
    int r = ((s_col_grid0 >> 11) & 0x1F) * v / 100;
    int g = ((s_col_grid0 >> 5)  & 0x3F) * v / 100;
    int b = ( s_col_grid0        & 0x1F) * v / 100;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void draw_overview(const nav_frame_t *f)
{
    const espnav_config_t *cfg = config_get();
    const int ax = OV_AX, ay = 160, aw = OV_AW, ah = 80;
    const uint16_t gc_minor = grid_color(cfg->grid_bright, 0);
    const uint16_t gc_major = grid_color(cfg->grid_bright, 25);

    /* 【M1.3】行程图区半透明暗底衬：先压暗再画网格，网格线才有对比可看
     *   （此前这里没有任何背景，靠整屏清屏黑；接入地图底图后必须自己铺一层） */
    if (cfg->scrim_on) fb_dim_rect(ax, ay, ax + aw - 1, ay + ah - 1, cfg->scrim_route);

    /* 网格：次格线每 10px / 主格线每 50px；线宽固定 1px = ESP 屏 1 物理像素 */
    for (int x = ax; x < ax + aw; x += 10)
        fb_line(x, ay, x, ay + ah - 1, ((x - ax) % 50 == 0) ? gc_major : gc_minor);
    for (int y = ay; y < ay + ah; y += 10)
        fb_line(ax, y, ax + aw - 1, y, ((y - ay) % 50 == 0) ? gc_major : gc_minor);
    for (int i = 0; i + 1 < f->overview_n; i++) {
        int x0, y0, x1, y1;
        ov_to_px(&f->overview[i], &x0, &y0);
        ov_to_px(&f->overview[i + 1], &x1, &y1);
        /* 3 倍粗（用户要求）：偏移画 3 条 */
        /* 【M2.4】行程图轨迹线颜色来自 App 配置（默认亮蓝 #5CB0FF = 0x5D9F）。
         * 用户 2026-09-19 反馈"原紫色对比度不好，改用蓝色系"。 */
        fb_line(x0, y0, x1, y1, s_col_track);
        fb_line(x0, y0 - 1, x1, y1 - 1, s_col_track);
        fb_line(x0 + 1, y0, x1 + 1, y1, s_col_track);
    }
    /* 起终点标记：轨迹首点=起点(绿)、末点=终点(红)；当前位置仍为黄点（App 实时更新） */
    if (f->overview_n >= 2) {
        int sx, sy, tx, ty;
        ov_to_px(&f->overview[0], &sx, &sy);
        ov_to_px(&f->overview[f->overview_n - 1], &tx, &ty);
        fb_fill_rect(sx - 4, sy - 4, sx + 4, sy + 4, RGB565_GREEN);
        fb_fill_rect(tx - 4, ty - 4, tx + 4, ty + 4, RGB565_RED);
    }
    if (f->overview_dot_valid) {
        int dx, dy;
        ov_to_px(&f->overview_dot, &dx, &dy);
        fb_fill_rect(dx - 4, dy - 4, dx + 4, dy + 4, RGB565_YELLOW);
    }
    draw_north8(ax + OV_OX + OV_BOX + 6, ay + 2);        /* 「北」紧跟内容右侧 */
    {   /* 十字线：位于“北”字下方；竖线上端带向上小箭头指向北 */
        const int vx = ax + OV_OX + OV_BOX + 10;    /* 8px 北字中心 */
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
/* 按【画布像素】判断是否在屏内（pt_in_screen 判的是协议点，这里判投影后的像素） */
static bool px_in_screen(int x, int y)
{
    return x >= 0 && x < FB_W && y >= 0 && y < FB_H;
}

static void road_path(const npt_t *pts, int n)
{
    if (n < 2) return;
    gpt_t pr[NAV_MAX_PTS];
    for (int i = 0; i < n; i++) { gpt_t g = { (float)pts[i].x, (float)pts[i].y }; pr[i] = geo_proj_pt(g); }
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i + 1 < n; i++) {
            int x0 = (int)pr[i].x, y0 = (int)pr[i].y, x1 = (int)pr[i+1].x, y1 = (int)pr[i+1].y;
            /* 越界一律按【投影后】的坐标判断：此前误用未投影的模板坐标，判的和画的不是同一套数 */
            if (!px_in_screen(x0, y0) || !px_in_screen(x1, y1)) continue;
            if (pass == 0) { fb_line(x0, y0, x1, y1, DBG_ROADPATH); fb_line(x0, y0+1, x1, y1+1, DBG_ROADPATH); }
            else           { fb_line(x0, y0, x1, y1, DBG_ROADPATH); fb_line(x0+1, y0, x1+1, y1, DBG_ROADPATH); }
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

/* 【M7】电量图标 + 百分比：画在 y≈100..159 的左侧空闲区（罗盘条以下、统计区以上），
 * 有底图时叠在地图上，所以用"白壳 + 黑腔 + 彩色电量"，任何背景都看得清；
 * 未接电池（battery_present()==false）时完全不画。 */
static void draw_battery(void)
{
    if (!battery_present()) return;
    int pct = battery_pct();
    if (pct < 0) return;

    /* 【M7.1】位置：时间行（y=135..157，右对齐 x=316）的**正上方**，右边缘同样对齐 316。
     * 于是整块（图标 + 百分比）从右往左排：先量文字宽度，再倒推图标起点。 */
    const int bw = 22, bh = 11;               /* 电池外壳 22×11 */
    const int gap = 5;
    const int right = 316;                    /* 与时间行右边缘对齐 */
    const int by = 112;                       /* 时间行上方（时间底衬从 y=135 开始）*/

    char buf[16];                             /* 够放 "100%" + NUL */
    snprintf(buf, sizeof(buf), "%d%%", pct);
    int tw = font_text_width(buf);
    int bx = right - (bw + 2 + gap + tw);     /* 2 = 正极凸头宽度 */
    if (bx < 2) bx = 2;

    /* 底衬：与时间用同一个透明度参数，压在地图上也能看清 */
    const espnav_config_t *bcfg = config_get();
    if (bcfg->scrim_on) fb_dim_rect(bx - 4, by - 3, right, by + bh + 3, bcfg->scrim_clock);

    uint16_t col = (pct <= 15) ? RGB565_RED
                 : (pct <= 40) ? RGB565_YELLOW : s_col_main;

    fb_fill_rect(bx, by, bx + bw - 1, by + bh - 1, RGB565_WHITE);          /* 外壳 */
    fb_fill_rect(bx + 1, by + 1, bx + bw - 2, by + bh - 2, RGB565_BLACK);  /* 内腔 */
    int iw = (bw - 4) * pct / 100;
    if (iw > 0) fb_fill_rect(bx + 2, by + 2, bx + 2 + iw - 1, by + bh - 3, col);
    fb_fill_rect(bx + bw + 1, by + 3, bx + bw + 2, by + bh - 4, RGB565_WHITE); /* 正极凸头 */
    font_draw_text(bx + bw + 2 + gap, by, buf, s_col_main);
}

static void draw_frame(const nav_frame_t *f, float anim)
{
    const espnav_config_t *cfg = config_get();       /* 【M1.2/M1.3】叠加层可读性参数（App 下发） */
    if (!f || !f->valid) return;

    /* 【M2.4】从配置同步颜色（0 = 用默认兜底）——
     * 下面所有 PATH_GREEN / ROAD_GRAY 用法因此自动跟随 App 里的颜色设置。 */
    s_col_main  = cfg->col_main  ? cfg->col_main  : DEF_COL_MAIN;
    s_col_road  = cfg->col_road  ? cfg->col_road  : DEF_COL_ROAD;
    s_col_track = cfg->col_track ? cfg->col_track : DEF_COL_TRACK;
    s_col_grid0 = cfg->col_grid  ? cfg->col_grid  : DEF_COL_GRID;
    s_col_car   = cfg->col_car   ? cfg->col_car   : DEF_COL_CAR;
    s_col_hint  = cfg->col_hint  ? cfg->col_hint  : DEF_COL_HINT;

    /* 【M3.1】地图底图（App 截图通道）：① 有新图先解码（只在这里做 —— 符合"仅显示任务
     * 碰帧缓冲"的约定）② 从底图副本整屏恢复；③ 有底图时不再画模板/兜底路面，
     * 于是画面 = 地图截图 + 半透明底衬 + 罗盘/文字/行程图/时间。 */
    /* 【M7】App 关掉「推送地图底图」（SET_CONFIG img_on=false）→ 这里丢弃底图副本，
     * 画面立刻回到模板路面/行程图，而不是一直贴着最后一张截图。
     * 注意：map_image_clear() 只动 map_image 内部标志，不碰 framebuffer，在本任务调用安全。 */
    if (!cfg->img_on && map_image_has()) map_image_clear();
    map_image_apply();
    const bool has_img = map_image_has();
    if (has_img) map_image_restore();
    else         fb_clear(RGB565_BLACK);
    fb_line(0, 160, FB_W - 1, 160, RGB565_DGRAY);
    fb_line(OV_AX, 160, OV_AX, FB_H - 1, RGB565_DGRAY);

    if (has_img) {
        /* 底图已铺好：跳过全部路面绘制 */
    } else if (f->road.present) {
        draw_road(f, anim);                        /* M4：路况模板（road 字段驱动） */
    } else if (f->center_n < 2) {
        /* 兜底：无路况模板、且中心线不足 2 点（例如刚到达终点/App 只发了终点帧）→
         * 画一段默认竖直路面，避免整个路面区空白（用户实测"导航结束后道路图案完全消失"）。 */
        int qx[4] = { NAV_CX - NAV_NEAR_HALF, NAV_CX + NAV_NEAR_HALF,
                      NAV_CX + NAV_FAR_HALF,  NAV_CX - NAV_FAR_HALF };
        int qy[4] = { NAV_NEAR_Y, NAV_NEAR_Y, NAV_FAR_Y, NAV_FAR_Y };
        fb_fill_quad(qx, qy, ROAD_GRAY);
        fb_dashed_line_off(qx[0], qy[0], qx[3], qy[3], RGB565_WHITE, 8, 6, anim);
        fb_dashed_line_off(qx[1], qy[1], qx[2], qy[2], RGB565_WHITE, 8, 6, anim);
    } else {
        road_fill_screen(f->center_line, f->center_n, NAV_NEAR_HALF, NAV_FAR_HALF, ROAD_GRAY);
        road_edges_screen(f->center_line, f->center_n, NAV_NEAR_HALF, NAV_FAR_HALF, anim);
        /* 车道中线虚线（沿 centerLine 折线） */
        for (int i = 0; i + 1 < f->center_n; i++) {
            fb_dashed_line_off(f->center_line[i].x, f->center_line[i].y,
                               f->center_line[i + 1].x, f->center_line[i + 1].y,
                               DBG_CENTERLN, 5, 7, anim);
        }
    }
    /* 【用户要求删除】主视图上的"已走路径/未走路径"两条折线（蓝/红）：
     * 它们在屏幕上随车头方向旋转，效果不好，不再绘制。道路走向由「道路图案」表达。
     * 恢复：把两个 if (0) 去掉即可。 */
    if (0)
    for (int i = 0; i + 1 < f->past_n; i++) {
        if (!pt_in_screen(f->past_center[i]) || !pt_in_screen(f->past_center[i + 1])) continue;
        fb_line(f->past_center[i].x, f->past_center[i].y,
                f->past_center[i + 1].x, f->past_center[i + 1].y, DBG_PAST);
    }
    if (0)
    for (int i = 0; i + 1 < f->route_n; i++) {
        if (!pt_in_screen(f->route_center[i]) || !pt_in_screen(f->route_center[i + 1])) continue;
        fb_line(f->route_center[i].x, f->route_center[i].y,
                f->route_center[i + 1].x, f->route_center[i + 1].y, DBG_ROUTE);
    }
/* 车辆光标 */
    /* 【用户要求·临时暂停】车头恒定竖线不绘制，便于排查"车头附近折线漂移"到底是谁画的。
     * 恢复：把下面这行注释打开即可（原先为贯通 NAV_FAR_Y -> NAV_NEAR_Y 的绿色中轴线）。 */
    /* fb_line(NAV_CX, NAV_FAR_Y, NAV_CX, NAV_NEAR_Y, RGB565_GREEN); */
    /* 【M2.4】车头三角颜色来自 App 配置（s_col_car） */
    if (f->pos_valid) fb_triangle(f->pos.x, f->pos.y, 14, s_col_car);

#if RENDER_TEXT
    /* 【M1.2/M1.3】叠加层可读性：先铺四类半透明暗底衬（在内容之上、文字之下），
     * 文字再靠 1px 黑描边保住对比度。参数全部来自 App 的 SET_CONFIG：
     *   scrim_compass / scrim_text / scrim_route（draw_overview 内部自己铺）/ scrim_clock */
    if (cfg->scrim_on) {
        fb_dim_rect(0, 0, FB_W - 1, 21, cfg->scrim_compass);          /* 罗盘条：整行覆盖 */
        fb_dim_rect(0, 21, 209, 78, cfg->scrim_text);                 /* 路名/hint/距离 三行 */
        fb_dim_rect(0, 160, OV_AX - 1, FB_H - 1, cfg->scrim_text);    /* 左下统计 4 行 */
    }
    font_set_outline(true, RGB565_BLACK);                             /* 文字/罗盘统一带黑描边 */

    /* 罗盘 + 行程图 + 底部网络状态（不依赖 RENDER_TEXT 开关） */
    draw_compass(f);
    draw_net_status();
    draw_battery();                          /* 【M7】电量（未接电池时不画） */
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
            /* 【M2.4】提示/警示行颜色来自 App 配置（s_col_hint） */
            draw_line_scroll(2, 4, 84, nb, s_col_hint, 316);
        }

        int km = (int)(f->total_dist / 1000);
        int frac = (int)((f->total_dist % 1000) / 100);
        snprintf(buf, sizeof(buf), "全程 %d.%dkm 已完%u%%", km, frac, (unsigned)f->progress_pct);
        font_draw_text(6, 164, buf, PATH_GREEN);

        snprintf(buf, sizeof(buf), "耗时 %u min", (unsigned)f->elapsed_min);
        font_draw_text(6, 182, buf, PATH_GREEN);

        snprintf(buf, sizeof(buf), "预计到达 %s", f->eta_time);
        font_draw_text(6, 200, buf, PATH_GREEN);

        /* 【M1.2/M2.6】时间（时:分:秒）：主视图右下角，右对齐 x=316、y=138。
         * ESP 无 RTC —— 字符串由 App 每秒下发。来源优先级：
         *   ① 本帧 NAV_FRAME.clock（导航中）
         *   ② 最近一次 CLOCK 报文的缓存 s_clock ← M2.6 补
         * ② 必须有：固件启动时 app_main 会设置 demo 帧（s_have=1），因此"已连接但未导航"
         * 时渲染的是 draw_frame(&s_cur)，render_nav_tick 的待机分支根本不会执行。 */
        const char *clock_txt = f->clock[0] ? f->clock : s_clock;
        if (clock_txt[0]) {
            int cw = font_text_width(clock_txt);
            int tx = 316 - cw;
            if (tx < 0) tx = 0;
            if (cfg->scrim_on) fb_dim_rect(tx - 4, 135, 316, 157, cfg->scrim_clock);
            font_draw_text(tx, 138, clock_txt, PATH_GREEN);
        }

        /* 北向标记统一由 draw_overview() 绘制（固定表示行程图方向），此处不再重复 */
    }
    font_set_outline(false, 0);              /* 描边仅作用于导航画面的文字层 */

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
        /* 【M2.6】待机画面也显示时间：用最近一次 CLOCK 报文的值（App 连接后每秒下发） */
        memcpy(empty.clock, s_clock, sizeof(empty.clock));
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
    w = font_text_width("EspNav v1.3.1") * 3;
    font_draw_text_scaled((FB_W - w) / 2, 40, "EspNav v1.3.1", PATH_GREEN, 3);
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
