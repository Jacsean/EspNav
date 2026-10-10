#pragma once
#include <stdint.h>
#include <stddef.h>

/* 帧重组器：按换行符(0x0A)从字节流切出完整 JSON 行（协议 V1.10 §5）。
 * 线性滚动缓冲（≤4096B，脏数据超限自动清空）。 */
/* 【M16.4】行缓冲改为**动态分配**：
 * NAV_FRAME 是"一行超长 JSON"（road 模板 / 中心线 / 未走·已走路径 / 行程图点集），
 * 原来固定 4096B，一旦整行超过就被**整帧丢弃** —— APK 无感知，ESP 表现为"完全没反应"
 * （一直停在"等待连接"）。另外不能只在栈上加大：client_task 的任务栈只有 8192。
 * 由 frame_parser_init() 分配（优先 PSRAM），frame_parser_deinit() 释放。 */
#define FRAME_MAX_LEN 16384

typedef struct {
    uint8_t *buf;              /* 动态缓冲（由 init 分配） */
    size_t   cap;              /* 缓冲容量 */
    size_t   len;
    uint32_t overflow_count;   /* 缓冲溢出(脏数据)次数 */
} frame_parser_t;

typedef void (*frame_line_cb)(const char *line, size_t len, void *ctx);

void frame_parser_init(frame_parser_t *fp);
/* 【M16.4】释放缓冲（连接断开时调用；可重复调用） */
void frame_parser_deinit(frame_parser_t *fp);
/* 喂入一段字节；每切出一条完整行即回调一次（不含行尾换行符）。 */
void frame_parser_feed(frame_parser_t *fp, const uint8_t *data, size_t len,
                       frame_line_cb cb, void *ctx);
