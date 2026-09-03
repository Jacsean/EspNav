#include "font.h"
#include "esp_log.h"
static const char *TAG = "font";
void font_init(void) { ESP_LOGI(TAG, "GB2312 字库 占位（M1/M2 加载 flash 只读分区）"); }
int font_draw_text(uint16_t x, uint16_t y, const char *utf8, uint16_t color)
{ (void)x;(void)y;(void)utf8;(void)color; return 0; }
