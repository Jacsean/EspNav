#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 协议 V1.10 NAV_FRAME payload -> 渲染结构（点数上限 16，协议 §3.1） */
#define NAV_MAX_PTS 16
#define NAV_HINT_MAX 48
#define NAV_ETA_MAX 8

typedef struct { int16_t x, y; } npt_t;

typedef struct {
    int16_t  heading;
    int16_t  turn_dist;
    char     hint[NAV_HINT_MAX + 1];
    int32_t  total_dist;
    uint8_t  progress_pct;
    uint16_t elapsed_min;
    char     eta_time[NAV_ETA_MAX + 1];

    npt_t    center_line[NAV_MAX_PTS];  int center_n;
    npt_t    past_center[NAV_MAX_PTS];  int past_n;
    npt_t    route_center[NAV_MAX_PTS]; int route_n;
    npt_t    pos;                       bool pos_valid;
    npt_t    overview[NAV_MAX_PTS];     int  overview_n;
    npt_t    overview_dot;              bool overview_dot_valid;

    bool     has_road;                  /* M2 暂不渲染模板，仅记录 */
    bool     valid;
} nav_frame_t;

const nav_frame_t *nav_frame_get(void);

/* 解析一行 JSON（NAV_FRAME/其它）；返回 true 表示已更新导航帧并需要重绘 */
bool nav_frame_on_json_line(const char *line, int len);
/* 供通信层在收到 PING 时回 PONG 等（可选） */
const char *nav_frame_last_msg_type(void);
