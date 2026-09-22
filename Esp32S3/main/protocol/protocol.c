#include "protocol.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "json_lite.h"
#include "nav_frame.h"
#include "config.h"
#include "battery.h"   /* 【M7】电量上报（vbat/batt） */
#include "lcd_driver.h"
#include "render_nav.h"
#include "map_image.h"     /* 【M3.1】IMG_BEGIN/CHUNK/END 的接收接口 */

static const char *TAG = "proto";
static uint32_t s_err = 0;

/* 应用层链路状态：只看“收到什么报文”（依据唯一，不依赖 TCP 连接是否抖动） */
#define LINK_TIMEOUT_US (5LL * 1000000LL)       /* 5 秒无任何报文 => App 不在了（异常断开兜底） */
static int64_t s_t_msg = 0;                     /* 最近收到任意报文的时刻 */
static int64_t s_t_nav = 0;                     /* 最近收到 NAV_FRAME 的时刻 */

/* 纯报文驱动：只要“App 的报文还在”就算已连接（画面内容完全由报文决定） */
int protocol_link_stage(void)
{
    int64_t now = esp_timer_get_time();
    if (s_t_msg == 0 || (now - s_t_msg) > LINK_TIMEOUT_US) return LINK_STAGE_WAIT_APP;
    return LINK_STAGE_CONNECTED;
}

void protocol_link_reset(void)
{
    s_t_msg = 0;
    s_t_nav = 0;
}

uint32_t protocol_err_count(void) { return s_err; }
void     protocol_err_reset(void) { s_err = 0; }

/* 回 DEV_STATUS（协议 §4.1）：配置 + 版本 + 错误计数
 * 【M1】追加 7 项叠加层可读性参数，App 保存后用 GET_CONFIG 回读校验一致性。
 * buf 由 240 提到 384：新增字段约 110 字符，避免 snprintf 截断（-Werror=format-truncation）。 */
static void send_dev_status(proto_send_fn send, void *ctx)
{
    const espnav_config_t *c = config_get();
    char buf[512];   /* 【M7】追加 img_on 等字段后防截断（原 384 是 M1 时的余量）*/
    snprintf(buf, sizeof(buf),
             "{\"msg_type\":\"DEV_STATUS\",\"payload\":{"
             "\"lcd_brightness\":%u,\"dash_speed\":%u,\"anim_enable\":%s,"
             "\"popup_timeout\":%u,"
             "\"scrim_on\":%s,\"scrim_compass\":%u,\"scrim_text\":%u,"
             "\"scrim_route\":%u,\"scrim_clock\":%u,\"grid_bright\":%u,\"map_area\":%u,"
             "\"img_on\":%s,\"screen_flip_y\":%s,"
             "\"vbat\":%d,\"batt\":%d,"
             "\"firmware_ver\":\"%s\",\"err\":%lu}}\n",
             (unsigned)c->lcd_brightness, (unsigned)c->dash_speed,
             c->anim_enable ? "true" : "false", (unsigned)c->popup_timeout,
             c->scrim_on ? "true" : "false",
             (unsigned)c->scrim_compass, (unsigned)c->scrim_text,
             (unsigned)c->scrim_route, (unsigned)c->scrim_clock,
             (unsigned)c->grid_bright, (unsigned)c->map_area,
             c->img_on ? "true" : "false", c->screen_flip_y ? "true" : "false",
             (int)battery_mv(), battery_pct(),
             c->firmware_ver, (unsigned long)s_err);
    if (send) send(buf, ctx);
    ESP_LOGI(TAG, "TX DEV_STATUS bright=%u dash=%u anim=%d popup=%u scrim=%d/%u,%u,%u,%u grid=%u area=%u ver=%s err=%lu",
             (unsigned)c->lcd_brightness, (unsigned)c->dash_speed, (int)c->anim_enable,
             (unsigned)c->popup_timeout, (int)c->scrim_on,
             (unsigned)c->scrim_compass, (unsigned)c->scrim_text,
             (unsigned)c->scrim_route, (unsigned)c->scrim_clock,
             (unsigned)c->grid_bright, (unsigned)c->map_area,
             c->firmware_ver, (unsigned long)s_err);
}

/* SET_CONFIG（协议 §3.2）：逐项应用，立即生效 */
static void apply_set_config(const char *line)
{
    int v = 0;
    bool on = false;

    if (jl_get_int(line, "lcd_brightness", -1, &v) && v >= 0) {
        config_set_brightness((uint8_t)v);
        const lcd_driver_ops_t *ops = lcd_driver_ops();
        if (ops && ops->backlight) ops->backlight((uint8_t)v);
        ESP_LOGI(TAG, "apply lcd_brightness=%d", v);
    }
    if (jl_get_int(line, "dash_speed", -1, &v) && v >= 0) {
        config_set_dash_speed((uint8_t)v);          /* 渲染 tick 每帧读 config，下一帧即生效 */
        ESP_LOGI(TAG, "apply dash_speed=%d", v);
    }
    if (jl_get_bool(line, "anim_enable", &on)) {
        config_set_anim_enable(on);                 /* false = 静态虚线（不推进相位） */
        ESP_LOGI(TAG, "apply anim_enable=%d", (int)on);
    }
    if (jl_get_int(line, "popup_timeout", -1, &v) && v >= 0) {
        config_set_popup_timeout((uint8_t)v);       /* 弹窗层 M7 使用 */
        ESP_LOGI(TAG, "apply popup_timeout=%d", v);
    }
    /* ---- M1：叠加层可读性（App 设置项下发；渲染每帧读 config，下一帧即生效）---- */
    if (jl_get_bool(line, "scrim_on", &on)) {
        config_set_scrim_on(on);
        ESP_LOGI(TAG, "apply scrim_on=%d", (int)on);
    }
    if (jl_get_int(line, "scrim_compass", -1, &v) && v >= 0) {
        config_set_scrim_compass((uint8_t)v);
        ESP_LOGI(TAG, "apply scrim_compass=%d", v);
    }
    if (jl_get_int(line, "scrim_text", -1, &v) && v >= 0) {
        config_set_scrim_text((uint8_t)v);
        ESP_LOGI(TAG, "apply scrim_text=%d", v);
    }
    if (jl_get_int(line, "scrim_route", -1, &v) && v >= 0) {
        config_set_scrim_route((uint8_t)v);
        ESP_LOGI(TAG, "apply scrim_route=%d", v);
    }
    if (jl_get_int(line, "scrim_clock", -1, &v) && v >= 0) {
        config_set_scrim_clock((uint8_t)v);
        ESP_LOGI(TAG, "apply scrim_clock=%d", v);
    }
    if (jl_get_int(line, "grid_bright", -1, &v) && v >= 0) {
        config_set_grid_bright((uint8_t)v);
        ESP_LOGI(TAG, "apply grid_bright=%d", v);
    }
    if (jl_get_int(line, "map_area", -1, &v) && v >= 0) {
        config_set_map_area((uint8_t)v);
        ESP_LOGI(TAG, "apply map_area=%d", v);
    }
    /* ---- 【M2.4】ESP 屏颜色（0 = 用固件默认色；渲染端每帧读 config）---- */
    if (jl_get_int(line, "col_main", -1, &v) && v >= 0) {
        config_set_col_main((uint16_t)v);
        ESP_LOGI(TAG, "apply col_main=%d", v);
    }
    if (jl_get_int(line, "col_track", -1, &v) && v >= 0) {
        config_set_col_track((uint16_t)v);
        ESP_LOGI(TAG, "apply col_track=%d", v);
    }
    if (jl_get_int(line, "col_grid", -1, &v) && v >= 0) {
        config_set_col_grid((uint16_t)v);
        ESP_LOGI(TAG, "apply col_grid=%d", v);
    }
    if (jl_get_int(line, "col_road", -1, &v) && v >= 0) {
        config_set_col_road((uint16_t)v);
        ESP_LOGI(TAG, "apply col_road=%d", v);
    }
    if (jl_get_int(line, "col_car", -1, &v) && v >= 0) {
        config_set_col_car((uint16_t)v);
        ESP_LOGI(TAG, "apply col_car=%d", v);
    }
    if (jl_get_int(line, "col_hint", -1, &v) && v >= 0) {
        config_set_col_hint((uint16_t)v);
        ESP_LOGI(TAG, "apply col_hint=%d", v);
    }
    /* ---- 【M2.5】分光镜 HUD：整屏水平镜像（App 设置项，默认开）---- */
    if (jl_get_bool(line, "screen_flip", &on)) {
        config_set_screen_flip(on);
        lcd_ili9341_set_flip_x(on);              /* 直接作用于驱动，下次刷屏即生效 */
        ESP_LOGI(TAG, "apply screen_flip=%d", (int)on);
    }
    /* ---- 【M9】整屏垂直镜像：与水平翻转独立，可单独开、也可同时开 ---- */
    if (jl_get_bool(line, "screen_flip_y", &on)) {
        config_set_screen_flip_y(on);
        lcd_ili9341_set_flip_y(on);              /* 直接作用于驱动，下次刷屏即生效 */
        ESP_LOGI(TAG, "apply screen_flip_y=%d", (int)on);
    }
    /* ---- 【M7】APK 地图底图总开关：false = 丢弃底图回原始导航模式 ----
     * 只改配置；渲染侧（draw_frame，仅显示任务碰帧缓冲）看到 false 时自行 map_image_clear()，
     * 因此这里不跨线程调用渲染侧接口。 */
    if (jl_get_bool(line, "img_on", &on)) {
        config_set_img_on(on);
        ESP_LOGI(TAG, "apply img_on=%d (0=drop map image, back to template road)", (int)on);
    }
}

void protocol_handle(const char *line, int len, proto_send_fn send, void *ctx)
{
    char mt[20];

    if (!line || len <= 0) return;
    if (!jl_get_str(line, "msg_type", mt, sizeof(mt))) {
        s_err++;                                     /* 非法帧计数（协议 §4.1 err） */
        ESP_LOGW(TAG, "RX len=%d no msg_type -> err=%lu | %.60s", len, (unsigned long)s_err, line);
        return;
    }
    ESP_LOGI(TAG, "RX len=%d type=%s", len, mt);     /* 探针：每条收到的报文都留痕（NUL 已由 frame_parser 补齐） */

    s_t_msg = esp_timer_get_time();                  /* 任何合法报文都算“App 在”（BYE 除外，见上） */

    if (!strcmp(mt, "BYE")) {                        /* 主动断开：立即回“等待 App” */
        protocol_link_reset();
        ESP_LOGI(TAG, "RX BYE -> 立即回到“等待手机App连接”");
        return;
    }
    if (!strcmp(mt, "HELLO")) {                      /* 握手：App 上线声明 */
        const espnav_config_t *c = config_get();
        char hb[128];
        snprintf(hb, sizeof(hb),
                 "{\"msg_type\":\"HELLO_ACK\",\"payload\":{\"firmware_ver\":\"%s\"}}\n",
                 c->firmware_ver);
        if (send) send(hb, ctx);
        ESP_LOGI(TAG, "RX HELLO -> TX HELLO_ACK ver=%s", c->firmware_ver);
        return;
    }
    if (!strcmp(mt, "NAV_FRAME")) {                  /* 仅缓存，渲染由显示任务完成 */
        s_t_nav = s_t_msg;
        nav_frame_on_json_line(line, len);
        return;
    }
    if (!strcmp(mt, "CLOCK")) {                      /* 【M2.6】时间下发（ESP 无 RTC）：待机/导航画面都显示 */
        char cb[NAV_CLOCK_MAX + 1];
        if (jl_get_str(line, "clock", cb, sizeof(cb))) {
            render_nav_set_clock(cb);
            ESP_LOGI(TAG, "RX CLOCK %s", cb);
        }
        return;
    }
    if (!strcmp(mt, "PING")) {
        int ts = 0, v = 0;
        jl_get_int(line, "ts", 0, &v);
        ts = v;
        char buf[96];
        snprintf(buf, sizeof(buf), "{\"msg_type\":\"PONG\",\"payload\":{\"ts\":%d}}\n", ts);
        if (send) send(buf, ctx);
        ESP_LOGI(TAG, "TX PONG ts=%d", ts);
        return;
    }
    if (!strcmp(mt, "GET_CONFIG")) {                  /* §3.3 -> DEV_STATUS，并清零 err */
        send_dev_status(send, ctx);
        s_err = 0;
        return;
    }
    if (!strcmp(mt, "SET_CONFIG")) {
        apply_set_config(line);
        send_dev_status(send, ctx);                   /* 回状态作为“已生效”确认（协议未定义 ACK） */
        return;
    }
    if (!strcmp(mt, "CLEAR_SCREEN")) {                /* §3.4 黑屏待机 */
        render_nav_clear();
        ESP_LOGI(TAG, "CLEAR_SCREEN done");
        return;
    }
    if (!strcmp(mt, "POPUP_MSG") || !strcmp(mt, "POPUP_CLOSE")) {
        ESP_LOGW(TAG, "%s 尚未实现（M7 弹窗层），已忽略", mt);
        return;
    }
    /* ---- 【M3.1】地图截图通道：接收侧只累积 base64 字节 + 置标志，
     * 不碰 framebuffer（解码/贴图由显示任务在 draw_frame 里做，见 fault_log F5 线程约定）---- */
    if (!strcmp(mt, "IMG_BEGIN")) {
        int seq = 0, w = 0, h = 0, x = 0, y = 0, bytes = 0;
        jl_get_int(line, "seq", 0, &seq);
        jl_get_int(line, "w", 320, &w);
        jl_get_int(line, "h", 240, &h);
        jl_get_int(line, "x", 0, &x);
        jl_get_int(line, "y", 0, &y);
        jl_get_int(line, "bytes", 0, &bytes);
        map_image_begin(seq, w, h, x, y, bytes);
        return;
    }
    if (!strcmp(mt, "IMG_CHUNK")) {
        static char b64[MAP_IMG_B64_MAX];               /* static：不占 TCP 任务栈 */
        int seq = 0;
        jl_get_int(line, "seq", 0, &seq);
        if (jl_get_str(line, "d", b64, sizeof(b64))) map_image_chunk(seq, b64);
        else ESP_LOGW(TAG, "IMG_CHUNK 缺 d 字段");
        return;
    }
    if (!strcmp(mt, "IMG_END")) {
        int seq = 0;
        jl_get_int(line, "seq", 0, &seq);
        map_image_end(seq);
        return;
    }
    ESP_LOGD(TAG, "ignore unknown msg_type=%s（协议 §6.2）", mt);
}
