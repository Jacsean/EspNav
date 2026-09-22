/*
 * audio_out.c — 【M13】音频输出：I2S std TX + ES8311 + 功放使能
 *
 * 采样格式：16 kHz / 16 bit / 单声道（Philips 标准）
 * MCLK    ：16 kHz × 256 = 4.096 MHz（经 GPIO4 送给 ES8311）
 * 功放    ：GPIO1 低电平使能；初始化时先关（避免上电"啪"声），出声前打开
 */
#include "audio_out.h"

#include <string.h>

#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "es8311.h"

static const char *TAG = "audio_out";

#define PIN_PA_EN       1               /* 功放使能，低有效 */
#define PIN_I2C_SCL     15
#define PIN_I2C_SDA     16

static i2s_chan_handle_t s_tx;
static bool              s_ready;
static bool              s_pa_on;

static void pa_enable(bool on)
{
    gpio_set_level((gpio_num_t)PIN_PA_EN, on ? 0 : 1);   /* 低电平使能 */
    s_pa_on = on;
}

esp_err_t audio_out_init(void)
{
    if (s_ready) return ESP_OK;

    /* ---- 功放使能脚：先关 ---- */
    gpio_config_t g = {
        .pin_bit_mask = 1ULL << PIN_PA_EN,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&g);
    pa_enable(false);

    /* ---- I2S：主机 + 只开 TX（本项目不录音，DIN 不接）---- */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_4,
            .bclk = GPIO_NUM_5,
            .ws   = GPIO_NUM_7,
            .dout = GPIO_NUM_8,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;   /* 16k*256 = 4.096MHz */
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx));
    ESP_LOGI(TAG, "I2S 就绪：%d Hz / 16bit / mono，MCLK=GPIO4 BCLK=GPIO5 WS=GPIO7 DOUT=GPIO8",
             AUDIO_SAMPLE_RATE);

    /* ---- ES8311（I2C）----
     * 失败也**不 abort**：I2S 侧保持可用，便于换线/换地址后继续排查。 */
    esp_err_t e = es8311_init(PIN_I2C_SCL, PIN_I2C_SDA, AUDIO_SAMPLE_RATE);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 初始化失败：%s —— I2S 已就绪但不会出声", esp_err_to_name(e));
    }

    /* ---- 打开功放 ---- */
    pa_enable(true);

    s_ready = true;
    ESP_LOGI(TAG, "audio_out 初始化完成（PA 使能脚 GPIO%d 已拉低）", PIN_PA_EN);
    return ESP_OK;
}

void audio_out_set_volume(int vol_0_100)
{
    es8311_set_volume(vol_0_100);
}

int audio_out_write(const void *pcm, int bytes)
{
    if (!s_tx || !pcm || bytes <= 0) return 0;
    if (!s_pa_on) pa_enable(true);          /* 出声前确保功放已开 */

    const uint8_t *p = (const uint8_t *)pcm;
    int off = 0;
    while (off < bytes) {
        size_t w = 0;
        esp_err_t e = i2s_channel_write(s_tx, p + off, (size_t)(bytes - off), &w, 1000);
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "i2s_channel_write 失败：%s（已写 %d/%d 字节）",
                     esp_err_to_name(e), off, bytes);
            break;
        }
        if (w == 0) break;
        off += (int)w;
    }
    return off;
}

/* 【第 1 批自检】方波"嘀"声 ×3 —— 不用 libm，便于确认链路是否通 */
void audio_out_test_tone(void)
{
    enum { N = AUDIO_SAMPLE_RATE * 2 / 5 };         /* 0.4 秒 */
    static int16_t buf[N];
    const int half = AUDIO_SAMPLE_RATE / 880;        /* 880Hz 方波半周期样本数 */
    if (half < 1) return;
    for (int i = 0; i < N; i++) {
        buf[i] = ((i / (half > 0 ? half : 1)) % 2) ? 9000 : -9000;
    }
    for (int k = 0; k < 3; k++) {
        int n = audio_out_write(buf, (int)sizeof(buf));
        ESP_LOGI(TAG, "测试音 #%d：写入 %d 字节（%d 样本）", k + 1, n, N);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
