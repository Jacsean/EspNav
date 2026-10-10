/*
 * audio_rx.c — 【M13-2】接收 App 下发的 TTS 语音 PCM，写入播放缓冲
 *
 * 线程约定：
 *   · 接收侧（protocol.c / TCP 任务）：base64 解码 → xStreamBufferSend
 *   · 播放侧（audio_out.c 的 audio_play_task）：xStreamBufferReceive → I2S
 *   xStreamBuffer 本身线程安全；本模块不碰 framebuffer。
 *
 * 缓冲满时只等 20ms 就放弃（宁可丢极少量数据，也不让 TCP 任务卡住）。
 */
#include "audio_rx.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

#include "b64.h"

static const char *TAG = "audio_rx";

#define CHUNK_RAW_MAX   1200    /* 与 App 端 MapShot/音频分块一致的原始字节上限 */

static StreamBufferHandle_t s_sb;
static int s_seq = -1;
static int s_decl = 0;
static int s_got = 0;

void audio_rx_init(void)
{
    if (s_sb) return;
    s_sb = xStreamBufferCreate(AUDIO_RX_BUF_BYTES, 1);
    if (!s_sb) {
        ESP_LOGE(TAG, "创建播放缓冲失败");
        return;
    }
    ESP_LOGI(TAG, "播放缓冲就绪：%d 字节（约 %.1f 秒 @16kHz/16bit/mono）",
             AUDIO_RX_BUF_BYTES, (double)AUDIO_RX_BUF_BYTES / (16000.0 * 2.0));
}

void audio_rx_begin(int seq, int rate, int bits, int ch, int bytes)
{
    audio_rx_init();
    if (!s_sb) return;

    if (rate != 16000 || bits != 16 || ch != 1) {
        ESP_LOGW(TAG, "不支持的音频格式 %dHz/%dbit/%dch（本固件只接受 16000/16/1）",
                 rate, bits, ch);
        s_seq = -1;
        return;
    }

    /* 新音频到来先清空旧数据：导航提示音"新的覆盖旧的"更符合直觉 */
    xStreamBufferReset(s_sb);
    s_seq = seq;
    s_decl = bytes;
    s_got = 0;
    ESP_LOGI(TAG, "开始接收语音：seq=%d 声明 %d 字节", seq, bytes);
}

bool audio_rx_chunk(int seq, const char *b64)
{
    if (!s_sb || !b64 || seq != s_seq) return false;

    uint8_t out[CHUNK_RAW_MAX];
    int n = b64_decode(b64, out, (int)sizeof(out));
    if (n <= 0) {
        ESP_LOGW(TAG, "块解码为空");
        return false;
    }
    size_t w = xStreamBufferSend(s_sb, out, (size_t)n, pdMS_TO_TICKS(20));
    s_got += (int)w;
    if (w < (size_t)n) {
        ESP_LOGW(TAG, "播放缓冲满，丢弃 %d 字节（累计收到 %d/%d）",
                 (int)((size_t)n - w), s_got, s_decl);
    }
    return true;
}

bool audio_rx_end(int seq)
{
    if (seq != s_seq) return false;
    ESP_LOGI(TAG, "语音接收完成：seq=%d %d/%d 字节", seq, s_got, s_decl);
    s_seq = -1;
    return true;
}

size_t audio_rx_take(void *dst, size_t max, uint32_t timeout_ms)
{
    if (!s_sb) return 0;
    return xStreamBufferReceive(s_sb, dst, max, pdMS_TO_TICKS(timeout_ms));
}

size_t audio_rx_pending(void)
{
    return s_sb ? xStreamBufferBytesAvailable(s_sb) : 0;
}
