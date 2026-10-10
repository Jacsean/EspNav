#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* ================= 【M13】ES8311 音频编解码 IC（只做 I2C 寄存器配置）=================
 * 硬件（ES3N28P 板载 ES8311 + 功放 IC）：
 *   I2C ：GPIO16 = SDA，GPIO15 = SCL（与电容触摸 FT6336G / 扩展 I2C 共用总线；
 *         本项目不使用触摸，因此不会争用总线）
 *   I2S ：GPIO4 = MCLK，GPIO5 = BCLK，GPIO7 = LRCK(WS)，GPIO8 = DOUT，GPIO6 = DIN
 *   功放：GPIO1 = 使能（**低电平有效**）
 *
 * 角色：ESP32-S3 作 I2S **主机**（提供 MCLK/BCLK/WS），ES8311 作 **从机**。
 * 采样率：16 kHz / 16 bit / 单声道；MCLK = 16 kHz × 256 = 4.096 MHz。
 * ============================================================================= */

/** 初始化 I2C 总线并配置 ES8311（幂等）。sample_rate 目前支持 16000。
 *  scl_gpio/sda_gpio：I2C 引脚（本项目 = 15 / 16）。 */
esp_err_t es8311_init(int scl_gpio, int sda_gpio, int sample_rate);

/** 设置 DAC 音量（0-100；0 = 静音，100 ≈ 0 dB）。 */
void es8311_set_volume(int vol_0_100);

/** 是否已成功配置（诊断用）。 */
bool es8311_is_ready(void);

/** 回读关键寄存器并与期望值比对（**没有喇叭时，这是确认"配置真的写进芯片"的唯一证据**）。
 *  输出形如：R[0x01]=0x3F (期望 0x3F) OK  内部时钟全开，最后汇总 x 项符合 / y 项不符。 */
void es8311_dump_regs(void);
