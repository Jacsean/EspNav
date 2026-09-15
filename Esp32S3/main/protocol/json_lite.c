#include "json_lite.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char *skip_ws(const char *p)
{
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

/* 定位 "key" 的值起点；未找到返回 NULL */
static const char *find_value(const char *json, const char *key)
{
    size_t klen = strlen(key);
    const char *p = json;
    while ((p = strchr(p, 0x22)) != NULL) {          /* 0x22 = '"' */
        if (strncmp(p + 1, key, klen) == 0 && p[1 + klen] == (char)0x22) {
            const char *q = skip_ws(p + 2 + klen);
            if (*q == ':') return skip_ws(q + 1);
        }
        p++;
    }
    return NULL;
}

bool jl_has_key(const char *json, const char *key)
{
    return json && key && find_value(json, key) != NULL;
}

bool jl_get_int(const char *json, const char *key, int def, int *out)
{
    const char *v = find_value(json, key);
    if (out) *out = def;
    if (!v) return false;
    char *end = NULL;
    double d = strtod(v, &end);
    if (end == v) return false;
    if (out) *out = (int)d;
    return true;
}

bool jl_get_str(const char *json, const char *key, char *out, int outsz)
{
    const char *v = find_value(json, key);
    if (!v || *v != (char)0x22 || outsz <= 0) return false;
    v++;
    int i = 0;
    while (*v && *v != (char)0x22 && i < outsz - 1) {
        if ((unsigned char)*v == 0x5C && v[1]) v++;   /* 0x5C = '\' 简单去转义 */
        out[i++] = *v++;
    }
    out[i] = 0;
    return true;
}

int jl_get_pts16(const char *json, const char *key, int16_t *out_xy, int max)
{
    const char *p = find_value(json, key);
    if (!p || *p != '[' || !out_xy || max <= 0) return 0;
    p++;
    int n = 0;
    while (*p && n < max) {
        p = skip_ws(p);
        if (*p == ',') { p++; continue; }     /* 元素之间的分隔逗号 */
        if (*p == ']') break;
        if (*p != '[') break;
        p++;
        char *end = NULL;
        double x = strtod(skip_ws(p), &end);
        if (end == skip_ws(p)) break;
        p = skip_ws(end);
        if (*p == ',') p++;
        end = NULL;
        double y = strtod(skip_ws(p), &end);
        if (end == skip_ws(p)) break;
        p = skip_ws(end);
        out_xy[n * 2] = (int16_t)x;
        out_xy[n * 2 + 1] = (int16_t)y;
        n++;
        if (*p == ']') p++;
    }
    return n;
}

bool jl_get_pair(const char *json, const char *key, int *x, int *y)
{
    const char *p = find_value(json, key);
    if (!p || *p != '[' || !x || !y) return false;
    p++;
    char *end = NULL;
    const char *q = skip_ws(p);
    double vx = strtod(q, &end);
    if (end == q) return false;
    p = skip_ws(end);
    if (*p == ',') p++;
    end = NULL;
    q = skip_ws(p);
    double vy = strtod(q, &end);
    if (end == q) return false;
    *x = (int)vx;
    *y = (int)vy;
    return true;
}

int jl_get_str_array(const char *json, const char *key, char *out, int outw, int max)
{
    const char *p = find_value(json, key);
    if (!p || *p != '[' || !out || outw <= 1 || max <= 0) return 0;
    p++;
    int n = 0;
    while (*p && n < max) {
        p = skip_ws(p);
        if (*p == ',') { p++; continue; }
        if (*p == ']') break;
        if (*p != (char)0x22) break;
        p++;
        int i = 0;
        char *dst = out + (size_t)n * outw;
        while (*p && *p != (char)0x22 && i < outw - 1) dst[i++] = *p++;
        dst[i] = 0;
        if (*p == (char)0x22) p++;
        n++;
    }
    return n;
}
