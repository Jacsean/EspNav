#pragma once
#include <stdint.h>

/* 几何常量与离散化（模板几何规格 V0.3 §5.3 / nav_sim_v2.html）——C 版与 JS 1:1 */
#define GEO_PROJ_CX      160
#define GEO_PROJ_Y_NEAR  130
#define GEO_PROJ_Y_FAR   20
#define GEO_PROJ_S_NEAR  1.0f
#define GEO_PROJ_S_FAR   0.24f
#define GEO_TPL_MAIN     62
#define GEO_TPL_SIDE     35
#define GEO_RBT_CX       160
#define GEO_RBT_CY       84
#define GEO_RBT_R        58
#define GEO_RBT_B        0.40f
#define GEO_RBT_A_IN     0.66f
#define GEO_RBT_B_IN     0.17f

void geo_init(void);
/* 深度 y 处的水平缩放（PROJ 透视） */
float geo_scale_at(float y);
