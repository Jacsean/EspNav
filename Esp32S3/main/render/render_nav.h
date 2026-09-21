#pragma once
#include "nav_frame.h"
/* 导航渲染（技术路径 §2.4）：M1 先实现直行透视条带；模板 readRoad 于 M4 接入 */
void render_nav_init(void);
/* 开机画面（B 科技版，用户确认）：stage 0=启动中 1=连WiFi 2=等待App；sub 为副状态行（可 NULL） */
void render_nav_boot(int stage, const char *sub);
void render_nav_demo(void);          /* 内置样例-直行（与 nav_sim_v2.html 参数一致） */
/* M3：帧到达时 set（仅缓存），显示任务周期调用 tick（含按 dash_speed 的虚线流动动画） */
void render_nav_set_frame(const nav_frame_t *f);
/* 已收到的帧计数（显示任务据此判断“数据是否真的在流动”，避免仅凭 TCP 连接切换画面） */
uint32_t render_nav_frame_count(void);
void render_nav_tick(float dt);
/* 【M2.6】时间（时:分:秒）由 App 的 CLOCK 报文下发（ESP 无 RTC）。
 * 待机画面（已连接、未导航）也显示时间 —— 因为 NAV_FRAME.clock 只在导航时才有。 */
void render_nav_set_clock(const char *hhmmss);
/* 立即用当前缓存帧渲染一次（收帧时调用，保证帧必上屏） */
void render_nav_draw_now(void);
void render_nav_frame(const nav_frame_t *f);   /* 立即渲染一帧（调试用；会触碰 SPI，仅允许显示任务调用） */
/* CLEAR_SCREEN / 链路断开：请求黑屏待机（实际清屏由显示任务执行，SPI 只在该任务访问） */
void render_nav_clear(void);
/* 链路断开（协议 §6.7）：保留最后画面并在中部显示“信号中断”，不清屏 */
void render_nav_link_lost(void);
