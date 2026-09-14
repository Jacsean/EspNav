#include "lcd_driver.h"
#include "lcd_pin_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "lcd_driver";

void lcd_ili9341_init(void);
void lcd_ili9341_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void lcd_ili9341_fill(uint16_t color);
void lcd_ili9341_backlight(uint8_t pct);

static void ops_init(void) { lcd_ili9341_init(); }

static const lcd_driver_ops_t s_ops = {
    .init = ops_init,
    .set_window = lcd_ili9341_set_window,
    .push_pixels = 0,
    .backlight = lcd_ili9341_backlight,
};

void lcd_driver_init(void)
{
    ESP_LOGI(TAG, "driver = ILI9341 (候选配置 A；若实物为 ST7789 需改初始化)");
    lcd_ili9341_init();
}

const lcd_driver_ops_t *lcd_driver_ops(void) { return &s_ops; }

void lcd_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    (void)x0; (void)y0; (void)x1; (void)y1; (void)color;   /* TODO(M1): 区域填充 */
}

/* M1 点亮探测：全屏色条轮换（红->绿->蓝->白->黑），串口同步打印，便于用户反馈屏幕现象 */
void lcd_probe_color_cycle(void)
{
    /* 慢速自检：红->绿->蓝，每色 2 秒（便于肉眼确认）；含单色填充耗时日志 */
    const struct { const char *name; uint16_t color; } seq[] = {
        { "RED", 0xF800 }, { "GREEN", 0x07E0 }, { "BLUE", 0x001F },
    };
    for (int i = 0; i < 3; i++) {
        int64_t t0 = esp_timer_get_time();
        ESP_LOGI(TAG, "fill %s ...", seq[i].name);
        lcd_ili9341_fill(seq[i].color);
        int64_t ms = (esp_timer_get_time() - t0) / 1000;
        ESP_LOGI(TAG, "fill %s done (%lld ms)", seq[i].name, (long long)ms);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    ESP_LOGI(TAG, "probe cycle done");
}
