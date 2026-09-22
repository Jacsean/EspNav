#pragma once
#include <stdbool.h>
#include "esp_err.h"

/* ================= 【M13】音频输出（I2S → ES8311 → 功放）=================
 * 链路：PCM(16kHz/16bit/单声道) → I2S std TX → ES8311(DAC) → 功放 IC → 喇叭
 * 引脚（用户提供）：
 *   GPIO1  功放使能（**低电平使能**）
 *   GPIO4  I2S MCLK      GPIO5  I2S BCLK
 *   GPIO7  I2S LRCK/WS   GPIO8  I2S DOUT      GPIO6  I2S DIN（不录音，未用）
 *   GPIO15 I2C SCL       GPIO16 I2C SDA（与触摸共用总线）
 * ======================================================================= */

#define AUDIO_SAMPLE_RATE   16000
#define AUDIO_CHANNELS      1           /* 单声道 */

/** 初始化 I2S + ES8311 + 功放（幂等）。 */
esp_err_t audio_out_init(void);

/** 设置音量（0-100）。 */
void audio_out_set_volume(int vol_0_100);

/** 播放一段 PCM（16bit 单声道，阻塞直到写完；内部有超时保护）。返回实际写入字节数。 */
int audio_out_write(const void *pcm, int bytes);

/** 【第 1 批自检】播放三声测试音 —— 用来验证"声卡链路是否通"。 */
void audio_out_test_tone(void);
