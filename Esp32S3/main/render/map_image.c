#include "map_image.h"
#include "lcd_fb.h"
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "rom/tjpgd.h"        /* ESP32-S3 ROM 自带 tjpgd（JD_FORMAT=0 → 输出 RGB888） */

static const char *TAG = "map_image";

/* ---- 接收状态（TCP 任务写 / 渲染任务读，用 s_pending 做单次交接）---- */
static uint8_t  s_jpg[MAP_IMG_MAX_BYTES];   /* base64 解出来的 JPEG 原始字节 */
static uint8_t  s_pool[8192];               /* tjpgd 工作池（R0.03 约需 3.5KB，8KB 留余量） */
static int      s_got       = 0;            /* 已收字节 */
static int      s_seq       = 0;            /* 当前图片序号（丢弃过期块） */
static int      s_decl_w    = FB_W;         /* 声明贴图尺寸（屏幕像素） */
static int      s_decl_h    = FB_H;
static int      s_decl_x    = 0;
static int      s_decl_y    = 0;
static volatile bool s_pending = false;     /* 收齐待解码 */
static bool     s_have      = false;        /* 已有可用底图 */

/* ---- 解码输出：整屏尺寸的 RGB565 副本（优先 PSRAM）---- */
static uint16_t *s_img = NULL;

/* ---- 解码过程中的流位置（infunc 顺序读）---- */
static int      s_in_pos = 0;
static int      s_in_len = 0;

/* ---------- base64 ---------- */
static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;                      /* '=' 或非法字符 */
}

/* 解码到 out（最多 outmax 字节），返回写入字节数 */
static int b64_decode(const char *s, uint8_t *out, int outmax)
{
    int n = 0, acc = 0, bits = 0;
    for (; *s; s++) {
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

/* ---------- tjpgd 回调 ---------- */
/* 输入：从 s_jpg 顺序喂字节（buff==NULL 表示跳过 nbyte 字节，同样推进位置） */
static UINT jd_in_func(JDEC *jd, BYTE *buff, UINT nbyte)
{
    (void)jd;
    if (s_in_pos >= s_in_len) return 0;
    UINT avail = (UINT)(s_in_len - s_in_pos);
    if (nbyte > avail) nbyte = avail;
    if (buff) memcpy(buff, s_jpg + s_in_pos, nbyte);
    s_in_pos += (int)nbyte;
    return nbyte;
}

/* 输出：RGB888 矩形块 → RGB565 写入 s_img（原图坐标即屏幕坐标，超出部分丢弃） */
static UINT jd_out_func(JDEC *jd, void *bitmap, JRECT *rect)
{
    (void)jd;
    const uint8_t *src = (const uint8_t *)bitmap;
    int w = (int)(rect->right - rect->left) + 1;
    int h = (int)(rect->bottom - rect->top) + 1;
    for (int y = 0; y < h; y++) {
        int gy = s_decl_y + (int)rect->top + y;
        const uint8_t *p = src + (size_t)y * (size_t)w * 3;
        if (gy < 0 || gy >= FB_H) continue;
        uint16_t *row = &s_img[(size_t)gy * FB_W];
        for (int x = 0; x < w; x++) {
            int gx = s_decl_x + (int)rect->left + x;
            if (gx < 0 || gx >= FB_W) continue;
            uint8_t r = p[x * 3 + 0], g = p[x * 3 + 1], b = p[x * 3 + 2];
            row[gx] = (uint16_t)(((uint16_t)(r & 0xF8) << 8) |
                                 ((uint16_t)(g & 0xFC) << 3) |
                                  (uint16_t)(b >> 3));
        }
    }
    return 1;                       /* 1 = 继续解码 */
}

/* ---------- 接收侧 ---------- */
void map_image_begin(int seq, int w, int h, int x, int y, int bytes)
{
    s_seq    = seq;
    s_got    = 0;
    s_decl_w = (w > 0 && w <= FB_W) ? w : FB_W;
    s_decl_h = (h > 0 && h <= FB_H) ? h : FB_H;
    s_decl_x = (x >= 0 && x + s_decl_w <= FB_W) ? x : 0;
    s_decl_y = (y >= 0 && y + s_decl_h <= FB_H) ? y : 0;
    s_in_len = (bytes > 0 && bytes <= MAP_IMG_MAX_BYTES) ? bytes : 0;
    s_pending = false;
    ESP_LOGI(TAG, "IMG_BEGIN seq=%d %dx%d at %d,%d bytes=%d", seq, s_decl_w, s_decl_h,
             s_decl_x, s_decl_y, bytes);
}

bool map_image_chunk(int seq, const char *b64)
{
    if (!b64 || seq != s_seq) return false;          /* 过期序号：丢弃 */
    int room = MAP_IMG_MAX_BYTES - s_got;
    if (room <= 0) { ESP_LOGW(TAG, "jpeg buffer full (%d)", s_got); return false; }
    int n = b64_decode(b64, s_jpg + s_got, room);
    s_got += n;
    return n > 0;
}

bool map_image_end(int seq)
{
    if (seq != s_seq) return false;
    if (s_got < 64) { ESP_LOGW(TAG, "IMG_END 数据不足 (%d B)，忽略", s_got); return false; }
    s_pending = true;                                /* 解码交给渲染侧（display_task） */
    ESP_LOGI(TAG, "IMG_END seq=%d 共 %d B -> 等待解码", seq, s_got);
    return true;
}

bool map_image_has(void) { return s_have; }

void map_image_clear(void)
{
    s_have = false;
    s_got = 0;
    s_pending = false;
    ESP_LOGI(TAG, "map image cleared");
}

/* ---------- 渲染侧 ---------- */
void map_image_apply(void)
{
    if (!s_pending) return;
    s_pending = false;

    if (!s_img) {
        size_t need = (size_t)FB_W * FB_H * 2;
        s_img = (uint16_t *)heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_img) {
            ESP_LOGI(TAG, "image layer %u B in PSRAM", (unsigned)need);
        } else {
            s_img = (uint16_t *)heap_caps_malloc(need, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            ESP_LOGW(TAG, "PSRAM 不可用，image layer %u B 回退内部 SRAM", (unsigned)need);
        }
        if (!s_img) { ESP_LOGE(TAG, "分配 image layer 失败，图片通道不可用"); return; }
    }

    /* 先清黑：避免上一张图的残留（也保证非图片区域是黑的） */
    memset(s_img, 0, (size_t)FB_W * FB_H * 2);

    s_in_pos = 0;
    s_in_len = s_got;
    static JDEC jdec;
    JRESULT r = jd_prepare(&jdec, jd_in_func, s_pool, (UINT)sizeof(s_pool), NULL);
    if (r != JDR_OK) { ESP_LOGE(TAG, "jd_prepare 失败: %d（JPEG 数据异常?）", (int)r); return; }
    ESP_LOGI(TAG, "JPEG %ux%u -> 贴到 %d,%d",
             (unsigned)jdec.width, (unsigned)jdec.height, s_decl_x, s_decl_y);
    r = jd_decomp(&jdec, jd_out_func, 0);            /* scale=0：1:1 */
    if (r != JDR_OK) { ESP_LOGE(TAG, "jd_decomp 失败: %d", (int)r); return; }

    s_have = true;
    ESP_LOGI(TAG, "底图就绪 (%dx%d @ %d,%d)", s_decl_w, s_decl_h, s_decl_x, s_decl_y);
}

void map_image_restore(void)
{
    if (!s_have || !s_img) return;
    uint16_t *fb = fb_get();
    if (!fb) return;
    memcpy(fb, s_img, (size_t)FB_W * FB_H * 2);
}
