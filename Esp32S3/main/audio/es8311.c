/*
 * es8311.c — 【M13】ES8311 音频编解码 IC 的 I2C 寄存器初始化
 *
 * 寄存器序列来源：Espressif 官方 i2s_es8311 示例（managed_components/espressif__es8311/es8311.c）
 * 的 es8311_init()，与 ESPHome / Rust es8311 crate 的实现一致：
 *   1) 复位    ：0x00=0x1F → 延时 20ms → 0x00=0x00 → 0x00=0x80（上电）
 *   2) 时钟    ：0x01=0x3F（内部时钟全开，MCLK 取自 MCLK 引脚）
 *                0x02..0x08 按 (MCLK, fs) 查系数表 —— 本项目 4.096MHz/16kHz
 *   3) 格式    ：0x00 清 BIT(6) → 从模式；0x09/0x0A 置 16bit（bits[3:2]=3 → 0x0C）
 *   4) 模拟上电：0x0D=0x01、0x0E=0x02、0x12=0x00、0x13=0x10、0x1C=0x6A、0x37=0x08
 *   5) 音量    ：0x32（0xBF = 0 dB）
 *
 * ⚠️ I2C 地址用 **7 位 0x18**（有些资料写 8 位 0x30，那是左移一位的写法）——
 *    地址写错是"寄存器全配完却没声音"的最常见原因，所以这里先 probe 一次。
 */
#include "es8311.h"

#include <string.h>

#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "es8311";

#define ES8311_ADDR_7BIT   0x18         /* = 8 位写法 0x30 */

static i2c_master_bus_handle_t  s_bus;
static i2c_master_dev_handle_t  s_dev;
static bool                     s_ready;

/* ---- 底层读写 ---- */
static esp_err_t es_wr(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

static esp_err_t es_wr_ok(uint8_t reg, uint8_t val)
{
    esp_err_t e = es_wr(reg, val);
    if (e != ESP_OK) ESP_LOGW(TAG, "write 0x%02X=0x%02X failed: %s", reg, val, esp_err_to_name(e));
    return e;
}

static esp_err_t es_rd(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

/* ---- 4.096MHz / 16kHz 的时钟寄存器值（系数表项 {4096000,16000,...}）---- */
static const uint8_t CLK_4096000_16000[][2] = {
    { 0x02, 0x00 },     /* DIV_PRE=1, MULT_PRE=1 */
    { 0x03, 0x10 },     /* fs_mode=单速, ADC OSR=16 */
    { 0x04, 0x10 },     /* DAC OSR=16 */
    { 0x05, 0x00 },     /* ADC/DAC divider = 1 */
    { 0x06, 0x03 },     /* BCLK divider = 4 */
    { 0x07, 0x00 },     /* LRCK 高字节 */
    { 0x08, 0xFF },     /* LRCK 低字节 */
};

esp_err_t es8311_init(int scl_gpio, int sda_gpio, int sample_rate)
{
    if (s_ready) return ESP_OK;
    if (sample_rate != 16000) {
        ESP_LOGW(TAG, "目前只支持 16kHz（需与 I2S 配置一致），收到 %d", sample_rate);
        return ESP_ERR_INVALID_ARG;
    }

    /* ---- 1) I2C 总线 + 设备 ---- */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t e = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(e));
        return e;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = ES8311_ADDR_7BIT,
        .scl_speed_hz    = 100000,
    };
    e = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(e));
        return e;
    }

    /* ---- 2) 先探测：地址错/接线错时给出明确结论，而不是"配了一通却没声" ---- */
    e = i2c_master_probe(s_bus, ES8311_ADDR_7BIT, 200);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 未应答（addr=0x%02X）：检查 SDA=GPIO%d / SCL=GPIO%d 与上拉",
                 ES8311_ADDR_7BIT, sda_gpio, scl_gpio);
        return e;
    }
    ESP_LOGI(TAG, "ES8311 已应答（addr=0x%02X, SDA=GPIO%d, SCL=GPIO%d）",
             ES8311_ADDR_7BIT, sda_gpio, scl_gpio);

    /* ---- 3) 复位并上电 ---- */
    es_wr_ok(0x00, 0x1F);
    vTaskDelay(pdMS_TO_TICKS(20));
    es_wr_ok(0x00, 0x00);
    es_wr_ok(0x00, 0x80);

    /* ---- 4) 时钟：0x01=0x3F 内部时钟全开；MCLK 来自 MCLK 引脚（bit7=0）---- */
    es_wr_ok(0x01, 0x3F);
    for (size_t i = 0; i < sizeof(CLK_4096000_16000) / sizeof(CLK_4096000_16000[0]); i++) {
        es_wr_ok(CLK_4096000_16000[i][0], CLK_4096000_16000[i][1]);
    }

    /* ---- 5) 格式：从模式 + 16bit ---- */
    uint8_t v = 0;
    if (i2c_master_transmit_receive(s_dev, (uint8_t[]){ 0x00 }, 1, &v, 1, 100) == ESP_OK) {
        v &= (uint8_t)~0x40;            /* 清 BIT(6) → I2S 从模式 */
        es_wr_ok(0x00, v);
    }
    es_wr_ok(0x09, 0x0C);               /* SDP In  ：16bit（bits[3:2]=3）*/
    es_wr_ok(0x0A, 0x0C);               /* SDP Out ：16bit */

    /* ---- 6) 模拟部分上电 + DAC 输出使能 ---- */
    es_wr_ok(0x0D, 0x01);               /* 上电模拟电路 */
    es_wr_ok(0x0E, 0x02);               /* 使能模拟 PGA / ADC 调制器 */
    es_wr_ok(0x12, 0x00);               /* 上电 DAC */
    es_wr_ok(0x13, 0x10);               /* 使能输出到 HP 驱动（= 功放输入）*/
    es_wr_ok(0x1C, 0x6A);               /* ADC EQ 旁路 / DC offset 取消 */
    es_wr_ok(0x37, 0x08);               /* 旁路 DAC EQ */

    /* ---- 7) 音量（默认 75 ≈ 0dB 附近）---- */
    es8311_set_volume(75);

    s_ready = true;
    ESP_LOGI(TAG, "ES8311 配置完成（16kHz/16bit，MCLK=4.096MHz）");
    es8311_dump_regs();                 /* 【M13-1.1】回读校验（无喇叭时的关键证据）*/
    return ESP_OK;
}

void es8311_set_volume(int vol_0_100)
{
    if (!s_dev) return;
    if (vol_0_100 < 0)   vol_0_100 = 0;
    if (vol_0_100 > 100) vol_0_100 = 100;
    /* 0 → 0x00（静音）；100 → 0xBF（0 dB），线性映射够用 */
    uint8_t reg = (uint8_t)((vol_0_100 * 0xBF) / 100);
    es_wr_ok(0x32, reg);
    ESP_LOGI(TAG, "音量 = %d%% (reg 0x32 = 0x%02X)", vol_0_100, reg);
}

bool es8311_is_ready(void) { return s_ready; }

/* 回读关键寄存器并与期望值比对。
 * 为什么需要：喇叭没到货时"听不到声音"无法区分是"没配好"还是"没喇叭"，
 * 而回读能直接证明配置已写进芯片 —— 软件侧正确性就不再依赖听觉。 */
void es8311_dump_regs(void)
{
    if (!s_dev) {
        ESP_LOGW(TAG, "寄存器回读跳过：I2C 设备未就绪");
        return;
    }
    static const struct { uint8_t reg; uint8_t exp; const char *name; } T[] = {
        { 0x00, 0x80, "CSM 上电 + 从模式" },
        { 0x01, 0x3F, "内部时钟全开(MCLK 取自 MCLK 脚)" },
        { 0x02, 0x00, "DIV_PRE=1 / MULT_PRE=1" },
        { 0x03, 0x10, "ADC OSR=16" },
        { 0x04, 0x10, "DAC OSR=16" },
        { 0x05, 0x00, "ADC/DAC divider=1" },
        { 0x06, 0x03, "BCLK divider=4" },
        { 0x07, 0x00, "LRCK 高字节" },
        { 0x08, 0xFF, "LRCK 低字节" },
        { 0x09, 0x0C, "SDP In = 16bit" },
        { 0x0A, 0x0C, "SDP Out = 16bit" },
        { 0x0D, 0x01, "模拟电路上电" },
        { 0x0E, 0x02, "PGA / ADC 调制器使能" },
        { 0x12, 0x00, "DAC 上电" },
        { 0x13, 0x10, "输出驱动使能(接功放)" },
    };
    int ok = 0, bad = 0;
    ESP_LOGI(TAG, "==== ES8311 寄存器回读（无喇叭时用它确认配置是否真的写入）====");
    for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); i++) {
        uint8_t v = 0;
        esp_err_t e = es_rd(T[i].reg, &v);
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "  R[0x%02X] 读取失败：%s", T[i].reg, esp_err_to_name(e));
            bad++;
            continue;
        }
        bool hit = (v == T[i].exp);
        if (hit) ok++; else bad++;
        ESP_LOGI(TAG, "  R[0x%02X]=0x%02X (期望 0x%02X) %s  %s",
                 T[i].reg, v, T[i].exp, hit ? "OK  " : "!!!!", T[i].name);
    }
    uint8_t vol = 0;
    if (es_rd(0x32, &vol) == ESP_OK) {
        ESP_LOGI(TAG, "  R[0x32]=0x%02X (DAC 音量，0xBF≈0dB)", vol);
    }
    ESP_LOGI(TAG, "==== 回读汇总：%d 项符合 / %d 项不符 %s ====",
             ok, bad, bad ? "<<< 有不符项，音频配置可能没写进去" : "(全部符合)");
}
