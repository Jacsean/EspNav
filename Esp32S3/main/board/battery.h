#pragma once
#include <stdbool.h>
#include <stdint.h>

/* ================= 【M7】板载电池电压检测 =================
 * 板型：ES3N28P / ES3C28P 系列（LCDWiki 2.8" IPS ESP32-S3 模块，ILI9341V，240×320）
 *   3.7V 锂电（JP1，1.25mm 2P）→ 2×200K 分压（1:2）→ BAT_ADC → **GPIO9** → ADC1_CH8
 *   4.2V 满电超过 ADC 量程（约 3.1V），所以必须分压：电池电压 = 实测电压 × 2。
 *   充电 TP4054（约 290mA）；插 Type-C 时 Q3(P-MOS) 切断电池供电，读数不受影响。
 *
 * 线程约定：battery_poll() 只由采样任务调用；渲染任务只读缓存（battery_mv/pct/present）。
 *          本模块不碰帧缓冲，因此不违反"只有 display_task 碰 framebuffer"的约定。
 * 未接电池：分压节点读数≈0 → battery_present()=false → 屏上不画电量图标。
 * ========================================================= */
void battery_init(void);      /* 初始化 ADC + 校准（幂等，可重复调用） */
void battery_poll(void);      /* 采样一次并更新缓存（建议 2s 一次） */
int  battery_mv(void);        /* 最近一次电池电压（mV；未接电池≈0） */
int  battery_pct(void);       /* 0-100；未接电池返回 -1 */
bool battery_present(void);   /* 是否检测到电池（电压 ≥ 2500mV） */
