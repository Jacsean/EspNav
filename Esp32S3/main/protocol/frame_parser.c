#include "frame_parser.h"

#include <stdlib.h>
#include "esp_heap_caps.h"     /* 【M16.4】优先用 PSRAM 分配行缓冲 */
#include <string.h>
#include "esp_log.h"

static const char *TAG = "frame_parser";

enum { NL = 0x0A, CR = 0x0D };   /* 换行/回车 */

void frame_parser_init(frame_parser_t *fp)
{
    if (!fp) return;
    if (!fp->buf) {
        /* 优先 PSRAM（不占用宝贵的内部 RAM），失败再回退内部 RAM */
        fp->buf = (uint8_t *)heap_caps_malloc(FRAME_MAX_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!fp->buf) fp->buf = (uint8_t *)malloc(FRAME_MAX_LEN);
        fp->cap = fp->buf ? FRAME_MAX_LEN : 0;
        if (fp->buf) ESP_LOGI(TAG, "行缓冲 %d B（动态分配，支持超长 NAV_FRAME）", FRAME_MAX_LEN);
        else         ESP_LOGE(TAG, "行缓冲分配失败（%d B）", FRAME_MAX_LEN);
    }
    fp->len = 0;
    fp->overflow_count = 0;
}

void frame_parser_deinit(frame_parser_t *fp)
{
    if (!fp) return;
    if (fp->buf) { free(fp->buf); fp->buf = NULL; }
    fp->cap = 0;
    fp->len = 0;
}

void frame_parser_feed(frame_parser_t *fp, const uint8_t *data, size_t len,
                       frame_line_cb cb, void *ctx)
{
    if (!fp || !data || !cb) return;
    for (size_t i = 0; i < len; i++) {
        uint8_t ch = data[i];
        if (fp->cap == 0) return;               /* 【M16.4】分配失败：直接不处理 */
        if (fp->len < fp->cap) {
            fp->buf[fp->len++] = ch;
        } else {
            /* 缓冲满仍未见换行：脏数据，清空重来（协议 §5）
             * 【M16.4】若你在这里看到告警，说明帧比 FRAME_MAX_LEN 还长 —— 请把这条日志发我。 */
            fp->overflow_count++;
            ESP_LOGW(TAG, "buffer overflow #%lu (%d B 仍未见换行), discard",
                     (unsigned long)fp->overflow_count, FRAME_MAX_LEN);
            fp->len = 0;
            continue;
        }
        if (ch == NL) {
            /* 去行尾 CR/空格后回调（不含换行符） */
            size_t n = fp->len - 1;
            while (n > 0 && (fp->buf[n-1] == CR || fp->buf[n-1] == 0x20)) n--;
            if (n > 0 && n < fp->cap) {
                fp->buf[n] = 0;      /* 关键：给回调的字符串补 NUL 结尾（json_lite 按 NUL 扫描） */
                cb((const char *)fp->buf, n, ctx);
            }
            fp->len = 0;
        }
    }
}
