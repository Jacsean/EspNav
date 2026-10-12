#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ================= 【M13-2】接收 App 下发的 TTS 语音 PCM =================
 * 链路：App(TextToSpeech) → 16kHz/16bit/单声道 PCM → base64 分块 →
 *       AUDIO_BEGIN / AUDIO_CHUNK / AUDIO_END → 本模块解码写入播放缓冲 →
 *       播放任务（audio_out.c）取走 → I2S → ES8311 → 功放。
 *
 * 线程约定（与 map_image 同样的思路）：
 *   · 接收侧（protocol.c / TCP 任务）只做 base64 解码 + 写入 StreamBuffer；
 *   · 播放侧（audio_play_task）只从 StreamBuffer 取；
 *   · 底层 xStreamBuffer 自带线程安全，本模块不碰 framebuffer。
 * ====================================================================== */

#define AUDIO_RX_BUF_BYTES   (16000 * 2 * 2)   /* 约 2 秒的 16bit 单声道数据 */

void   audio_rx_init(void);

/* --- 接收侧（protocol.c 调用）--- */
void   audio_rx_begin(int seq, int rate, int bits, int ch, int bytes);
bool   audio_rx_chunk(int seq, const char *b64);
bool   audio_rx_end(int seq);

/* --- 播放侧 --- */
size_t audio_rx_take(void *dst, size_t max, uint32_t timeout_ms);   /* 无数据返回 0 */
size_t audio_rx_pending(void);                                      /* 缓冲中剩余字节 */
