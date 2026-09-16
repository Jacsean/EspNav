#include "protocol.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "json_lite.h"
#include "nav_frame.h"
#include "config.h"
#include "lcd_driver.h"
#include "render_nav.h"

static const char *TAG = "proto";
static uint32_t s_err = 0;

/* 应用层链路状态：只看“收到什么报文”（依据唯一，不依赖 TCP 连接是否抖动） */
#define LINK_TIMEOUT_US (5LL * 1000000LL)       /* 5 秒无任何报文 => App 不在了（异常断开兜底） */
static int64_t s_t_msg = 0;                     /* 最近收到任意报文的时刻 */
static int64_t s_t_nav = 0;                     /* 最近收到 NAV_FRAME 的时刻 */

/* 简化后的 2 阶段：只要“App 的报文还在”就算已连接（有没有导航数据由渲染侧决定显示内容） */
int protocol_link_stage(void)
{
    int64_t now = esp_timer_get_time();
    if (s_t_msg == 0 || (now - s_t_msg) > LINK_TIMEOUT_US) return 1;   /* 1 = 等待 App */
    return 2;                                                          /* 2 = 已连接 */
}

void protocol_link_reset(void)
{
    s_t_msg = 0;
    s_t_nav = 0;
}

uint32_t protocol_err_count(void) { return s_err; }
void     protocol_err_reset(void) { s_err = 0; }

/* 回 DEV_STATUS（协议 §4.1）：配置 + 版本 + 错误计数 */
static void send_dev_status(proto_send_fn send, void *ctx)
{
    const espnav_config_t *c = config_get();
    char buf[240];
    snprintf(buf, sizeof(buf),
             "{\"msg_type\":\"DEV_STATUS\",\"payload\":{"
             "\"lcd_brightness\":%u,\"dash_speed\":%u,\"anim_enable\":%s,"
             "\"popup_timeout\":%u,\"firmware_ver\":\"%s\",\"err\":%lu}}\n",
             (unsigned)c->lcd_brightness, (unsigned)c->dash_speed,
             c->anim_enable ? "true" : "false", (unsigned)c->popup_timeout,
             c->firmware_ver, (unsigned long)s_err);
    if (send) send(buf, ctx);
    ESP_LOGI(TAG, "TX DEV_STATUS bright=%u dash=%u anim=%d popup=%u ver=%s err=%lu",
             (unsigned)c->lcd_brightness, (unsigned)c->dash_speed, (int)c->anim_enable,
             (unsigned)c->popup_timeout, c->firmware_ver, (unsigned long)s_err);
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
}

void protocol_handle(const char *line, int len, proto_send_fn send, void *ctx)
{
    char mt[20];

    if (!line || len <= 0) return;
    if (!jl_get_str(line, "msg_type", mt, sizeof(mt))) {
        s_err++;                                     /* 非法帧计数（协议 §4.1 err） */
        ESP_LOGW(TAG, "no msg_type -> err=%lu", (unsigned long)s_err);
        return;
    }

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
    ESP_LOGD(TAG, "ignore unknown msg_type=%s（协议 §6.2）", mt);
}
