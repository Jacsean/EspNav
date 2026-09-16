#include "nav_frame.h"
#include <stdio.h>
#include <string.h>
#include "json_lite.h"
#include "esp_log.h"
#include "render/render_nav.h"

static const char *TAG = "nav_frame";
static nav_frame_t s_nav;
static char s_last_type[16];

const nav_frame_t *nav_frame_get(void) { return &s_nav; }
const char *nav_frame_last_msg_type(void) { return s_last_type; }

static int get_i(const char *json, const char *k, int def)
{
    int v = def;
    (void)jl_get_int(json, k, def, &v);
    return v;
}

bool nav_frame_on_json_line(const char *line, int len)
{
    (void)len;
    char type[16] = { 0 };
    jl_get_str(line, "msg_type", type, sizeof(type));
    snprintf(s_last_type, sizeof(s_last_type), "%s", type);

    if (!jl_has_key(line, "payload")) {
        ESP_LOGW(TAG, "no payload / parse miss");
        return false;
    }

    memset(&s_nav, 0, sizeof(s_nav));
    s_nav.heading      = (int16_t)get_i(line, "heading", 0);
    s_nav.turn_dist    = (int16_t)get_i(line, "turn_dist", 0);
    s_nav.total_dist   = get_i(line, "total_dist", 0);
    s_nav.progress_pct = (uint8_t)get_i(line, "progress_pct", 0);
    s_nav.elapsed_min  = (uint16_t)get_i(line, "elapsed_min", 0);
    jl_get_str(line, "hint", s_nav.hint, sizeof(s_nav.hint));
    jl_get_str(line, "eta_time", s_nav.eta_time, sizeof(s_nav.eta_time));

    s_nav.center_n = jl_get_pts16(line, "centerLine", (int16_t *)s_nav.center_line, NAV_MAX_PTS);
    s_nav.past_n   = jl_get_pts16(line, "pastCenter", (int16_t *)s_nav.past_center, NAV_MAX_PTS);
    s_nav.route_n  = jl_get_pts16(line, "routeCenter", (int16_t *)s_nav.route_center, NAV_MAX_PTS);

    int px = 0, py = 0;
    if (jl_get_pair(line, "pos", &px, &py)) {          /* pos 是扁平 [x,y] */
        s_nav.pos.x = (int16_t)px;
        s_nav.pos.y = (int16_t)py;
        s_nav.pos_valid = true;
    }
    s_nav.overview_n = jl_get_pts16(line, "overview", (int16_t *)s_nav.overview, NAV_MAX_PTS);
    int ox = 0, oy = 0;
    if (jl_get_pair(line, "overview_dot", &ox, &oy)) {
        s_nav.overview_dot.x = (int16_t)ox;
        s_nav.overview_dot.y = (int16_t)oy;
        s_nav.overview_dot_valid = true;
    }
    jl_get_str(line, "roadName", s_nav.road_name, sizeof(s_nav.road_name));
    jl_get_str(line, "notice", s_nav.notice, sizeof(s_nav.notice));
    s_nav.has_road = jl_has_key(line, "road");
    if (s_nav.has_road) {
        nav_road_t *rd = &s_nav.road;
        rd->present = true;
        jl_get_str(line, "type", rd->type, sizeof(rd->type));
        jl_get_str(line, "dir", rd->dir, sizeof(rd->dir));
        rd->exits_n = jl_get_str_array(line, "exits", &rd->exits[0][0], 6, NAV_EXITS_MAX);
        rd->pts_n   = jl_get_pts16(line, "pts", (int16_t *)rd->pts, NAV_MAX_PTS);
        int v = 0;
        rd->cx   = jl_get_int(line, "cx",   160, &v) ? (int16_t)v : 160;
        rd->cy   = jl_get_int(line, "cy",    84, &v) ? (int16_t)v : 84;
        rd->r    = jl_get_int(line, "r",     58, &v) ? (int16_t)v : 58;
        rd->half = jl_get_int(line, "half",  62, &v) ? (int16_t)v : 62;
        ESP_LOGI(TAG, "road: type=%s dir=%s exits=%d pts=%d half=%d",
                 rd->type, rd->dir, rd->exits_n, rd->pts_n, (int)rd->half);
    }
    s_nav.valid = true;

    if (strcmp(type, "NAV_FRAME") == 0) {
        ESP_LOGI(TAG, "NAV_FRAME hint=%s dist=%d center=%d past=%d route=%d pos=%d,%d road=%d",
                 s_nav.hint, s_nav.turn_dist, s_nav.center_n, s_nav.past_n, s_nav.route_n,
                 (int)s_nav.pos.x, (int)s_nav.pos.y, (int)s_nav.has_road);
        render_nav_set_frame(&s_nav);   /* 仅缓存；渲染统一由显示任务 tick 完成（避免占用 TCP 任务栈） */
        return true;
    }
    ESP_LOGI(TAG, "msg_type=%s (未渲染)", type);
    return false;
}
