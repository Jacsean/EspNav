#include "lcd_driver.h"
#include "esp_log.h"

static const char *TAG = "lcd_driver";

static void ops_init(void)        { ESP_LOGI(TAG, "ILI9341 init 占位（M1：接线定稿后实现寄存器序列）"); }
static void ops_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) { (void)x0;(void)y0;(void)x1;(void)y1; }
static void ops_push_pixels(const uint16_t *d, uint32_t n) { (void)d;(void)n; }
static void ops_backlight(uint8_t p) { (void)p; }

static const lcd_driver_ops_t s_ops = {
    .init = ops_init, .set_window = ops_set_window,
    .push_pixels = ops_push_pixels, .backlight = ops_backlight,
};

void lcd_driver_init(void) { ESP_LOGI(TAG, "driver abstract ready"); }
const lcd_driver_ops_t *lcd_driver_ops(void) { return &s_ops; }
void lcd_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{ (void)x0;(void)y0;(void)x1;(void)y1;(void)color; }
