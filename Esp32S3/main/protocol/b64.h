#pragma once
#include <stdint.h>

/* ================= 【M13-2】base64 解码（公共实现）=================
 * 原先是 render/map_image.c 里的 static 函数；音频接收（AUDIO_CHUNK）也需要，
 * 于是提取到这里共用 —— 避免两份实现将来"改一处漏一处"。
 *
 * 特性：遇到 '=' 停止；自动跳过换行/空格等非表内字符（与 App 端 NO_WRAP 兼容）。
 * 返回：解出的字节数（超过 outmax 的部分被丢弃，但仍继续消费输入）。
 * ================================================================= */
int b64_decode(const char *s, uint8_t *out, int outmax);
