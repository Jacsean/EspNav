#pragma once
#include "nav_frame.h"
/* 导航渲染（技术路径 §2.4）：M1 先实现直行透视条带；模板 readRoad 于 M4 接入 */
void render_nav_init(void);
void render_nav_demo(void);          /* 内置样例-直行（与 nav_sim_v2.html 参数一致） */
/* M3：帧到达时 set（仅缓存），显示任务周期调用 tick（含按 dash_speed 的虚线流动动画） */
void render_nav_set_frame(const nav_frame_t *f);
void render_nav_tick(float dt);
/* 立即用当前缓存帧渲染一次（收帧时调用，保证帧必上屏） */
void render_nav_draw_now(void);
void render_nav_frame(const nav_frame_t *f);   /* 立即渲染一帧（保留，便于调试） */
