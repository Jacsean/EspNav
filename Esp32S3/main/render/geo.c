#include "geo.h"
#include "esp_log.h"

static const char *TAG = "geo";

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float geo_scale_at(float y)
{
    float t = (y - GEO_PROJ_Y_FAR) / (float)(GEO_PROJ_Y_NEAR - GEO_PROJ_Y_FAR);
    t = clampf(t, 0.0f, 1.0f);
    return GEO_PROJ_S_FAR + (GEO_PROJ_S_NEAR - GEO_PROJ_S_FAR) * t;
}

void geo_init(void)
{
    ESP_LOGI(TAG, "geo constants ready (PROJ/RBT 与 HTML V2 一致)");
}
