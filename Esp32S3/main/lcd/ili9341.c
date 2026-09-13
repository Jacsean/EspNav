/*
 * ili9341.c — ILI9341 (4-wire SPI, 240x320 -> 横屏 320x240) 驱动实现
 * 用途：M1 点亮探测。若实物为 ST7789，只需替换本文件的初始化序列与 MADCTL 组合。
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
static uint16_t s_line[LCD_W];            /* 行缓冲（一次 1 行 = 640B） */

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

/* 背光 PWM（LEDC） */
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
    uint32_t duty = (pct > 100 ? 100 : pct) * 255 / 100;
#if !LCD_BL_ACTIVE_HIGH
    duty = 255 - duty;
#endif
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void hw_reset(void)
{
#if LCD_PIN_RST >= 0
    gpio_set_direction(LCD_PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_PIN_RST, 0); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(LCD_PIN_RST, 1); vTaskDelay(pdMS_TO_TICKS(150));
#else
    vTaskDelay(pdMS_TO_TICKS(20));
#endif
}

static void ili9341_init_seq(void)
{
    lcd_cmd(0x01); vTaskDelay(pdMS_TO_TICKS(150));          /* SWRESET */
    lcd_cmd(0x11); vTaskDelay(pdMS_TO_TICKS(150));          /* SLPOUT */
    lcd_cmd(0x3A); lcd_data1(0x55);                         /* COLMOD: 16bit RGB565 */
    lcd_cmd(0x36); lcd_data1(0x28);                         /* MADCTL: 横屏(MV)+BGR */
    lcd_cmd(0x13);                                          /* NORON */
    lcd_cmd(0x29); vTaskDelay(pdMS_TO_TICKS(50));           /* DISPON */
}

void lcd_ili9341_init(void)
{
    ESP_LOGI(TAG, "init: SCLK=%d MOSI=%d MISO=%d CS=%d DC=%d RST=%d BL=%d @%dMHz",
             LCD_PIN_SCLK, LCD_PIN_MOSI, LCD_PIN_MISO, LCD_PIN_CS, LCD_PIN_DC,
             LCD_PIN_RST, LCD_PIN_BL, LCD_SPI_HZ / 1000000);

    gpio_set_direction(LCD_PIN_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(LCD_PIN_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_PIN_CS, 0);
    bl_init();

    spi_bus_config_t bus = {
        .mosi_io_num = LCD_PIN_MOSI, .miso_io_num = LCD_PIN_MISO, .sclk_io_num = LCD_PIN_SCLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = LCD_W * 2 + 8 };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t dev = {
        .clock_speed_hz = LCD_SPI_HZ, .mode = 0, .spics_io_num = -1, .queue_size = 4 };
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_SPI_HOST, &dev, &s_spi));

    hw_reset();
    ili9341_init_seq();
    bl_set(100);
    ESP_LOGI(TAG, "init done, backlight 100%%");
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
    uint16_t c = (color >> 8) | (color << 8);   /* SPI 大端 */
    for (int i = 0; i < LCD_W; i++) s_line[i] = c;
    lcd_ili9341_set_window(0, 0, LCD_W - 1, LCD_H - 1);
    gpio_set_level(LCD_PIN_DC, 1);
    for (int y = 0; y < LCD_H; y++) {
        spi_transaction_t t = { .length = LCD_W * 16, .tx_buffer = s_line };
        ESP_ERROR_CHECK(spi_device_transmit(s_spi, &t));
    }
}

void lcd_ili9341_backlight(uint8_t pct) { bl_set(pct); }
