/* b64.c — 公共 base64 解码（从 render/map_image.c 提取，实现未变）*/
#include "b64.h"

static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int b64_decode(const char *s, uint8_t *out, int outmax)
{
    int n = 0, acc = 0, bits = 0;
    for (; s && *s; s++) {
        if (*s == '=') break;
        int v = b64_val(*s);
        if (v < 0) continue;        /* 跳过换行/空白等 */
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n < outmax) out[n++] = (uint8_t)((acc >> bits) & 0xFF);
        }
    }
    return n;
}
