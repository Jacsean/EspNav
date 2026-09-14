/*
 * ili9341.c — 裸 SPI 驱动 + 商家提供的完整初始化序列（2026-09-14）
 * 板级（quandong-s3-dev 官方配置）：SCK=12 MOSI=11 DC=46 CS=10 BL=45(不反转)，无 MISO、无 RST
 * 横屏：MADCTL=0x68 (BGR|MY|MV)，INVON(0x21)，COLMOD=0x55
 */
#include "lcd_driver.h"
#include "lcd_pin_config.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_log.h"

static const char *TAG = "ili9341";
static spi_device_handle_t s_spi;
static uint16_t s_line[LCD_W];

/* ---------- 底层：命令/数据 ---------- */
static void lcd_cmd(uint8_t c)
{
    gpio_set_level(LCD_PIN_DC, 0);
    spi_transaction_t t = { .length = 8, .tx_buffer = &c };
    ESP_ERROR_CHECK(spi_device_transmit(s_spi, &t));
}
static void lcd_data(const uint8_t *d, size_t n)
{
    gpio_set_level(LCD_PIN_DC, 1);
    spi_transaction_t t = { .length = n * 8, .tx_buffer = d };
    ESP_ERROR_CHECK(spi_device_transmit(s_spi, &t));
}
static void lcd_data1(uint8_t d) { lcd_data(&d, 1); }
static void wr_reg(uint8_t reg) { lcd_cmd(reg); }
static void wr_data(uint8_t d) { lcd_data1(d); }

/* ---------- 背光 ---------- */
static void bl_init(void)
{
    ledc_timer_config_t tcfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
    ESP_ERROR_CHECK(ledc_timer_config(&tcfg));
    ledc_channel_config_t ccfg = {
        .gpio_num = LCD_PIN_BL, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE, .timer_sel = LEDC_TIMER_0, .duty = 0, .hpoint = 0 };
    ESP_ERROR_CHECK(ledc_channel_config(&ccfg));
}
static void bl_set(uint8_t pct)
{
    uint32_t duty = (uint32_t)(pct > 100 ? 100 : pct) * 255 / 100;
#if !LCD_BL_ACTIVE_HIGH
    duty = 255 - duty;
#endif
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/* ---------- 复位（本板无 RST 引脚：硬件复位 + 软复位兜底） ---------- */
static void lcd_reset(void)
{
#if LCD_PIN_RST >= 0
    gpio_set_direction(LCD_PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_PIN_RST, 1); vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(LCD_PIN_RST, 0); vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(LCD_PIN_RST, 1); vTaskDelay(pdMS_TO_TICKS(50));
#endif
    lcd_cmd(0x01);                       /* SWRESET */
    vTaskDelay(pdMS_TO_TICKS(150));
}

/* ---------- 商家提供的 ILI9341 完整初始化序列 ---------- */
static void ili9341_init_seq(void)
{
    wr_reg(0xCF); wr_data(0x00); wr_data(0xC1); wr_data(0x30);
    wr_reg(0xED); wr_data(0x64); wr_data(0x03); wr_data(0x12); wr_data(0x81);
    wr_reg(0xE8); wr_data(0x85); wr_data(0x00); wr_data(0x78);
    wr_reg(0xCB); wr_data(0x39); wr_data(0x2C); wr_data(0x00); wr_data(0x34); wr_data(0x02);
    wr_reg(0xF7); wr_data(0x20);
    wr_reg(0xEA); wr_data(0x00); wr_data(0x00);
    wr_reg(0xC0); wr_data(0x13);                       /* Power control 1 */
    wr_reg(0xC1); wr_data(0x13);                       /* Power control 2 */
    wr_reg(0xC5); wr_data(0x22); wr_data(0x35);        /* VCOM 1 */
    wr_reg(0xC7); wr_data(0xBD);                       /* VCOM 2 */
    wr_reg(0x11); vTaskDelay(pdMS_TO_TICKS(120));      /* SLPOUT（官方序列位置） */
    wr_reg(0x21);                                      /* INVON */
    wr_reg(0x36); wr_data(0xA0);                       /* MADCTL 横屏: MV|MY (RGB 序, 官方 esp_lcd 等价值) */
    wr_reg(0xB6); wr_data(0x0A); wr_data(0xA2);
    wr_reg(0x3A); wr_data(0x55);                       /* COLMOD 16bit */
    wr_reg(0xF6); wr_data(0x01); wr_data(0x30);
    wr_reg(0xB1); wr_data(0x00); wr_data(0x1B);
    wr_reg(0xF2); wr_data(0x00);
    wr_reg(0x26); wr_data(0x01);
    wr_reg(0xE0);                                     /* Positive Gamma */
    wr_data(0x0F); wr_data(0x35); wr_data(0x31); wr_data(0x0B); wr_data(0x0E); wr_data(0x06);
    wr_data(0x49); wr_data(0xA7); wr_data(0x33); wr_data(0x07); wr_data(0x0F); wr_data(0x03);
    wr_data(0x0C); wr_data(0x0A); wr_data(0x00);
    wr_reg(0xE1);                                     /* Negative Gamma */
    wr_data(0x00); wr_data(0x0A); wr_data(0x0F); wr_data(0x04); wr_data(0x11); wr_data(0x08);
    wr_data(0x36); wr_data(0x58); wr_data(0x4D); wr_data(0x07); wr_data(0x10); wr_data(0x0C);
    wr_data(0x32); wr_data(0x34); wr_data(0x0F);
    wr_reg(0x29); vTaskDelay(pdMS_TO_TICKS(50));       /* DISPON */
}

void lcd_ili9341_init(void)
{
    ESP_LOGI(TAG, "init: SCLK=%d MOSI=%d CS=%d DC=%d RST=%d BL=%d @%dMHz",
             LCD_PIN_SCLK, LCD_PIN_MOSI, LCD_PIN_CS, LCD_PIN_DC, LCD_PIN_RST, LCD_PIN_BL,
             LCD_SPI_HZ / 1000000);

    gpio_set_direction(LCD_PIN_DC, GPIO_MODE_OUTPUT);
    /* CS 交给 SPI 驱动逐笔控制（官方 esp_lcd 亦然）；此前“常拉低”在 0xF6 接口控制后可能失效 */
    bl_init();

    spi_bus_config_t bus = {
        .mosi_io_num = LCD_PIN_MOSI, .miso_io_num = -1, .sclk_io_num = LCD_PIN_SCLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = LCD_W * 2 + 8 };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t dev = {
        .clock_speed_hz = LCD_SPI_HZ, .mode = 0, .spics_io_num = LCD_PIN_CS, .queue_size = 4 };
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_SPI_HOST, &dev, &s_spi));

    lcd_reset();
    ili9341_init_seq();
    bl_set(100);
    ESP_LOGI(TAG, "init done (vendor init sequence), backlight 100%%");
}

void lcd_ili9341_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t b[4];
    lcd_cmd(0x2A); b[0] = x0 >> 8; b[1] = x0 & 0xFF; b[2] = x1 >> 8; b[3] = x1 & 0xFF; lcd_data(b, 4);
    lcd_cmd(0x2B); b[0] = y0 >> 8; b[1] = y0 & 0xFF; b[2] = y1 >> 8; b[3] = y1 & 0xFF; lcd_data(b, 4);
    lcd_cmd(0x2C);
}

void lcd_ili9341_fill(uint16_t color)
{
    uint16_t c = (uint16_t)((color >> 8) | (color << 8));
    for (int i = 0; i < LCD_W; i++) s_line[i] = c;
    lcd_ili9341_set_window(0, 0, LCD_W - 1, LCD_H - 1);
    gpio_set_level(LCD_PIN_DC, 1);
    for (int y = 0; y < LCD_H; y++) {
        spi_transaction_t t = { .length = LCD_W * 16, .tx_buffer = s_line };
        ESP_ERROR_CHECK(spi_device_transmit(s_spi, &t));
    }
}

void lcd_ili9341_backlight(uint8_t pct) { bl_set(pct); }

void lcd_ili9341_flush(const uint16_t *fb, int w, int h)
{
    if (!s_spi || !fb) return;
    if (w > LCD_W) w = LCD_W;
    if (h > LCD_H) h = LCD_H;
    lcd_ili9341_set_window(0, 0, (uint16_t)(w - 1), (uint16_t)(h - 1));
    gpio_set_level(LCD_PIN_DC, 1);
    for (int y = 0; y < h; y++) {
        const uint16_t *src = &fb[y * w];
        for (int x = 0; x < w; x++) {
            uint16_t c = src[x];
            s_line[x] = (uint16_t)((c >> 8) | (c << 8));
        }
        spi_transaction_t t = { .length = (size_t)w * 16, .tx_buffer = s_line };
        ESP_ERROR_CHECK(spi_device_transmit(s_spi, &t));
    }
}
