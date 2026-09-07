#pragma once
#include <stdint.h>
#include <stdbool.h>

/* 几何常量与离散化（模板几何规格 V0.3 §5.3 / nav_sim_v2.html）——C 版与 JS 1:1 */
#define GEO_PLANE_CX     160.0f   /* 屏幕中心 x */
#define GEO_PROJ_Y_NEAR  130.0f
#define GEO_PROJ_Y_FAR   20.0f
#define GEO_PROJ_S_NEAR  1.0f
#define GEO_PROJ_S_FAR   0.24f
#define GEO_TPL_MAIN     62.0f
#define GEO_TPL_SIDE     35.0f
#define GEO_RBT_CX       160.0f
#define GEO_RBT_CY       84.0f
#define GEO_RBT_R        58.0f
#define GEO_RBT_B        0.40f
#define GEO_RBT_A_IN     0.66f
#define GEO_RBT_B_IN     0.17f
#define GEO_RBT_RW       0.31f
#define GEO_RBT_S_IN     0.62f

/* 屏幕边界（支路到边截断用；与 v2 dirToEdge 一致） */
#define GEO_EDGE_LEFT    20.0f
#define GEO_EDGE_RIGHT   300.0f
#define GEO_EDGE_TOP     26.0f
#define GEO_EDGE_BOTTOM  134.0f

/* 方位 → 参数角(弧度)；参数与 JS AZM 一致：E0/SE45/S90/SW135/W180/NW225/N270/NE315 */
#define GEO_AZ_E   0.0f
#define GEO_AZ_SE  0.7853982f
#define GEO_AZ_S   1.5707963f
#define GEO_AZ_SW  2.3561945f
#define GEO_AZ_W   3.1415927f
#define GEO_AZ_NW  3.9269908f
#define GEO_AZ_N   4.7123890f
#define GEO_AZ_NE  5.4977871f

typedef struct { float x, y; } gpt_t;

void geo_init(void);

/* PROJ 透视：深度 y 处水平缩放 */
float geo_scale_at(float y);
/* 平面点 → 投影点（x 向 cx 收缩，y 不变） */
gpt_t geo_proj_pt(gpt_t p);

/* 中心线 → 路面左右边（fatten，与 v2 同构：proj 后法线 + 半宽随深度缩放）。
 * half0: 平面基准半宽；pts_in/out 容量见调用方（≤16 点，out ≤ 2*16+2）。 */
void geo_fatten(const gpt_t *pts_in, int n, float half0,
                gpt_t *out_l, gpt_t *out_r, int *out_n);

/* 椭圆参数角弧离散点（ellArcPts）：th0→th1 线性扫 n 段，返回 n+1 点 */
void geo_ell_arc_pts(float cx, float cy, float a, float b,
                     float th0, float th1, int n, gpt_t *out);

/* 方位名(如 'E','NE') → 参数角（未知返回 0） */
float geo_azimuth_rad(const char *name);

/* O2 比率表：由观感外环水平半轴 r 导出环岛观感参数（b/aIn/bIn/rw/sIn） */
void geo_rbt_metrics(float r, float *b, float *a_in, float *b_in,
                     float *rw, float *s_in);

/* 方向弧扫掠角：自南入口(thS)沿绕行方向(spin=±1)到目标出口角。
 * spin>0=角度增大(逆时针)；返回 0..2π 或 -2π..0（0 时回退 0.999*2π*spin）。 */
float geo_sweep_directed(float th_s, float th_t, int spin);
