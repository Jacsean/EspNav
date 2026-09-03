#pragma once
#include <stdint.h>
#include <stddef.h>

/* 帧重组器：按换行符(0x0A)从字节流切出完整 JSON 行（协议 V1.10 §5）。
 * 线性滚动缓冲（≤4096B，脏数据超限自动清空）。 */
typedef struct {
    uint8_t buf[4096];
    size_t  len;
} frame_parser_t;

typedef void (*frame_line_cb)(const char *line, size_t len, void *ctx);

void frame_parser_init(frame_parser_t *fp);
/* 喂入一段字节；每切出一条完整行即回调一次（不含行尾换行符）。 */
void frame_parser_feed(frame_parser_t *fp, const uint8_t *data, size_t len,
                       frame_line_cb cb, void *ctx);
