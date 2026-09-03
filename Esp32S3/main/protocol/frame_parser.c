#include "frame_parser.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "frame_parser";

void frame_parser_init(frame_parser_t *fp)
{
    if (fp) { fp->len = 0; }
}

void frame_parser_feed(frame_parser_t *fp, const uint8_t *data, size_t len,
                       frame_line_cb cb, void *ctx)
{
    if (!fp || !data || !cb) return;
    for (size_t i = 0; i < len; i++) {
        uint8_t ch = data[i];
        if (fp->len < sizeof(fp->buf)) {
            fp->buf[fp->len++] = ch;
        } else {
            /* 缓冲满仍未见换行：脏数据，清空重来（协议 §5） */
            ESP_LOGW(TAG, "buffer overflow, discard");
            fp->len = 0;
            continue;
        }
        if (ch == '
') {
            /* 去尾空白后回调（不含换行符） */
            size_t n = fp->len - 1;
            while (n > 0 && (fp->buf[n-1] == '' || fp->buf[n-1] == ' ')) n--;
            if (n > 0) cb((const char *)fp->buf, n, ctx);
            fp->len = 0;
        }
    }
}
