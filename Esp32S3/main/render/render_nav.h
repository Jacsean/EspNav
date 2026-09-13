#pragma once
#include "nav_frame.h"
/* 导航渲染（技术路径 §2.4）：M1 先实现直行透视条带；模板 readRoad 于 M4 接入 */
void render_nav_init(void);
void render_nav_demo(void);          /* 内置样例-直行（与 nav_sim_v2.html 参数一致） */
void render_nav_frame(const nav_frame_t *f);   /* 按协议帧渲染（直行条带；模板 M4） */
