#include "geo.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "geo";

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float pi2_(void) { return 6.2831853f; }

void geo_init(void)
{
    ESP_LOGI(TAG, "geo algorithms ready (fatten/proj/ellipse/sweep, 与 HTML V2 1:1)");
}

float geo_scale_at(float y)
{
    float t = (y - GEO_PROJ_Y_FAR) / (GEO_PROJ_Y_NEAR - GEO_PROJ_Y_FAR);
    t = clampf(t, 0.0f, 1.0f);
    return GEO_PROJ_S_FAR + (GEO_PROJ_S_NEAR - GEO_PROJ_S_FAR) * t;
}

gpt_t geo_proj_pt(gpt_t p)
{
    gpt_t r;
    r.x = GEO_PLANE_CX + (p.x - GEO_PLANE_CX) * geo_scale_at(p.y);
    r.y = p.y;
    return r;
}

void geo_fatten(const gpt_t *pts, int n, float half0,
                gpt_t *out_l, gpt_t *out_r, int *out_n)
{
    int i;
    if (!pts || n < 2 || !out_l || !out_r || !out_n) return;
    for (i = 0; i < n; i++) {
        const gpt_t *a = &pts[i > 0 ? i - 1 : 0];
        const gpt_t *b = &pts[i];
        const gpt_t *cc = &pts[i < n - 1 ? i + 1 : n - 1];
        gpt_t pa = geo_proj_pt(*a), pb = geo_proj_pt(*b), pc = geo_proj_pt(*cc);
        float dx = pc.x - pa.x, dy = pc.y - pa.y;
        float ang = atan2f(dy, dx) + 1.5707963f;   /* +90° */
        float nx = cosf(ang), ny = sinf(ang);
        float hw = half0 * geo_scale_at(b->y);
        out_l[i].x = pb.x + nx * hw;  out_l[i].y = pb.y + ny * hw;
        out_r[i].x = pb.x - nx * hw;  out_r[i].y = pb.y - ny * hw;
    }
    *out_n = n;
}

void geo_ell_arc_pts(float cx, float cy, float a, float b,
                     float th0, float th1, int n, gpt_t *out)
{
    int i;
    if (!out || n < 1) return;
    for (i = 0; i <= n; i++) {
        float th = th0 + (th1 - th0) * (float)i / (float)n;
        out[i].x = cx + a * cosf(th);
        out[i].y = cy + b * sinf(th);
    }
}

float geo_azimuth_rad(const char *name)
{
    if (!name) return 0.0f;
    switch (name[0]) {
    case 'E': return GEO_AZ_E;
    case 'S': return name[1] == 'E' ? GEO_AZ_SE : (name[1] == 'W' ? GEO_AZ_SW : GEO_AZ_S);
    case 'W': return GEO_AZ_W;
    case 'N': return name[1] == 'E' ? GEO_AZ_NE : (name[1] == 'W' ? GEO_AZ_NW : GEO_AZ_N);
    default:  return 0.0f;
    }
}

void geo_rbt_metrics(float r, float *b, float *a_in, float *b_in,
                     float *rw, float *s_in)
{
    if (b)    *b    = r * GEO_RBT_B;
    if (a_in) *a_in = r * GEO_RBT_A_IN;
    if (b_in) *b_in = r * GEO_RBT_B_IN;   /* 0.17*r -> 内岛垂直半轴 */
    if (rw)   *rw   = r * GEO_RBT_RW;
    if (s_in) *s_in = r * GEO_RBT_S_IN;
}

float geo_sweep_directed(float th_s, float th_t, int spin)
{
    float tau = pi2_();
    float d;
    if (spin > 0) {
        d = fmodf(th_t - th_s, tau);
        if (d < 0) d += tau;
    } else {
        d = fmodf(th_s - th_t, tau);
        if (d < 0) d += tau;
        d = -d;
    }
    if (d == 0.0f) d = (spin > 0 ? 1.0f : -1.0f) * tau * 0.999f;
    return d;
}
