#include "nav_frame.h"
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "render/render_nav.h"

static const char *TAG = "nav_frame";
static nav_frame_t s_nav;
static char s_last_type[16];

const nav_frame_t *nav_frame_get(void) { return &s_nav; }
const char *nav_frame_last_msg_type(void) { return s_last_type; }

static int parse_pts(const cJSON *arr, npt_t *out, int max)
{
    int n = 0;
    if (!cJSON_IsArray(arr)) return 0;
    const cJSON *it = NULL;
    cJSON_ArrayForEach(it, arr) {
        if (n >= max) break;
        if (!cJSON_IsArray(it)) continue;
        const cJSON *px = cJSON_GetArrayItem(it, 0);
        const cJSON *py = cJSON_GetArrayItem(it, 1);
        if (!cJSON_IsNumber(px) || !cJSON_IsNumber(py)) continue;
        out[n].x = (int16_t)px->valuedouble;
        out[n].y = (int16_t)py->valuedouble;
        n++;
    }
    return n;
}

static int get_int(const cJSON *o, const char *k, int def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? (int)v->valuedouble : def;
}

bool nav_frame_on_json_line(const char *line, int len)
{
    (void)len;
    cJSON *root = cJSON_Parse(line);
    if (!root) { ESP_LOGW(TAG, "JSON parse failed"); return false; }

    const cJSON *mt = cJSON_GetObjectItemCaseSensitive(root, "msg_type");
    const char *type = cJSON_IsString(mt) ? mt->valuestring : "";
    strncpy(s_last_type, type, sizeof(s_last_type) - 1);
    s_last_type[sizeof(s_last_type) - 1] = 0;

    const cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    if (!cJSON_IsObject(payload)) { cJSON_Delete(root); return false; }

    memset(&s_nav, 0, sizeof(s_nav));
    s_nav.heading      = (int16_t)get_int(payload, "heading", 0);
    s_nav.turn_dist    = (int16_t)get_int(payload, "turn_dist", 0);
    s_nav.total_dist   = get_int(payload, "total_dist", 0);
    s_nav.progress_pct = (uint8_t)get_int(payload, "progress_pct", 0);
    s_nav.elapsed_min  = (uint16_t)get_int(payload, "elapsed_min", 0);

    const cJSON *hint = cJSON_GetObjectItemCaseSensitive(payload, "hint");
    if (cJSON_IsString(hint)) {
        strncpy(s_nav.hint, hint->valuestring, NAV_HINT_MAX);
        s_nav.hint[NAV_HINT_MAX] = 0;
    }
    const cJSON *eta = cJSON_GetObjectItemCaseSensitive(payload, "eta_time");
    if (cJSON_IsString(eta)) {
        strncpy(s_nav.eta_time, eta->valuestring, NAV_ETA_MAX);
        s_nav.eta_time[NAV_ETA_MAX] = 0;
    }

    s_nav.center_n = parse_pts(cJSON_GetObjectItemCaseSensitive(payload, "centerLine"), s_nav.center_line, NAV_MAX_PTS);
    s_nav.past_n   = parse_pts(cJSON_GetObjectItemCaseSensitive(payload, "pastCenter"), s_nav.past_center, NAV_MAX_PTS);
    s_nav.route_n  = parse_pts(cJSON_GetObjectItemCaseSensitive(payload, "routeCenter"), s_nav.route_center, NAV_MAX_PTS);

    const cJSON *pos = cJSON_GetObjectItemCaseSensitive(payload, "pos");
    if (cJSON_IsArray(pos) && cJSON_GetArraySize(pos) >= 2) {
        s_nav.pos.x = (int16_t)cJSON_GetArrayItem(pos, 0)->valuedouble;
        s_nav.pos.y = (int16_t)cJSON_GetArrayItem(pos, 1)->valuedouble;
        s_nav.pos_valid = true;
    }
    s_nav.has_road = cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(payload, "road"));
    s_nav.valid = true;

    cJSON_Delete(root);

    if (strcmp(type, "NAV_FRAME") == 0) {
        ESP_LOGI(TAG, "NAV_FRAME: hint=\"%s\" dist=%d pts=%d road=%d", s_nav.hint, s_nav.turn_dist, s_nav.center_n, (int)s_nav.has_road);
        render_nav_frame(&s_nav);
        return true;
    }
    ESP_LOGI(TAG, "msg_type=%s (未渲染)", type);
    return false;
}
