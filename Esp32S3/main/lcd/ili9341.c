/*
 * ili9341.c — 改用 IDF 官方 esp_lcd + esp_lcd_ili9341 驱动（与出厂固件同路径）
 * 引脚按 quandong-s3-dev 官方板级配置：
 *   SCK=12 MOSI=11 DC=46 CS=10 BL=45(不反转)  无 MISO / 无 RST
 * 横屏：swap_xy=true, mirror_y=true（MADCTL=0xA0）；INVON 由 invert_color 处理。
 */
#include "lcd_driver.h"
#include "lcd_pin_config.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

static const char *TAG = "ili9341";
static esp_lcd_panel_io_handle_t s_io = NULL;
static esp_lcd_panel_handle_t    s_panel = NULL;
static uint16_t s_line[LCD_W];

/* ---------------- 背光 ---------------- */
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

/* ---------------- 初始化 ---------------- */
void lcd_ili9341_init(void)
{
    ESP_LOGI(TAG, "init(esp_lcd): SCLK=%d MOSI=%d CS=%d DC=%d BL=%d @%dMHz",
             LCD_PIN_SCLK, LCD_PIN_MOSI, LCD_PIN_CS, LCD_PIN_DC, LCD_PIN_BL, LCD_SPI_HZ / 1000000);

    bl_init();

    spi_bus_config_t bus = {
        .mosi_io_num = LCD_PIN_MOSI, .miso_io_num = -1, .sclk_io_num = LCD_PIN_SCLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = LCD_W * 2 * 8 };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = LCD_SPI_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, &s_io));

    esp_lcd_panel_dev_config_t dev_cfg = {
        .reset_gpio_num = -1,               /* 板上无独立 RST */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(s_io, &dev_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));   /* 反相型面板 */
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));        /* 横屏 */
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, false, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, 0, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    bl_set(100);
    ESP_LOGI(TAG, "init done (esp_lcd), backlight 100%%");
}

/* 兼容原接口：全屏填充 */
void lcd_ili9341_fill(uint16_t color)
{
    if (!s_panel) return;
    for (int i = 0; i < LCD_W; i++) s_line[i] = color;
    for (int y = 0; y < LCD_H; y++) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_W, y + 1, s_line));
    }
}

void lcd_ili9341_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    (void)x0; (void)y0; (void)x1; (void)y1;   /* esp_lcd 版按 draw_bitmap 直接指定区域 */
}

void lcd_ili9341_backlight(uint8_t pct) { bl_set(pct); }

/* 整屏推送（帧缓冲 -> 面板） */
void lcd_ili9341_flush(const uint16_t *fb, int w, int h)
{
    if (!s_panel || !fb) return;
    if (w > LCD_W) w = LCD_W;
    if (h > LCD_H) h = LCD_H;
    /* esp_lcd 需要大端 RGB565；逐行转换后发送 */
    for (int y = 0; y < h; y++) {
        const uint16_t *src = &fb[y * w];
        for (int x = 0; x < w; x++) {
            uint16_t c = src[x];
            s_line[x] = (uint16_t)((c >> 8) | (c << 8));
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, 0, y, w, y + 1, s_line));
    }
}

static void ops_init(void) { lcd_ili9341_init(); }
static const lcd_driver_ops_t s_ops = {
    .init = ops_init, .set_window = lcd_ili9341_set_window,
    .push_pixels = NULL, .backlight = lcd_ili9341_backlight,
};
