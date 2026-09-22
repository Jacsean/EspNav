/*
 * battery.c — 【M7】板载电池电压检测（BAT_ADC = GPIO9 = ADC1_CH8）
 *
 * 采样策略：一次采 9 个样本取中位数（抑制抖动/毛刺），默认 2s 采一次。
 * 换算：优先用 ADC 曲线拟合校准（eFuse 出厂校准值）；校准不可用时退化为按
 *       参考电压 3100mV / 12bit(4095) 粗略换算。
 */
#include "battery.h"

#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "battery";

#define BAT_ADC_UNIT      ADC_UNIT_1
#define BAT_ADC_CHANNEL   ADC_CHANNEL_8      /* ADC1_CH8 = GPIO9（板载 BAT_ADC 信号） */
#define BAT_ADC_ATTEN     ADC_ATTEN_DB_12    /* 量程约 0–3.1V（ESP32-S3 有效上限） */
#define BAT_DIVIDER       2                  /* 1:2 分压 → 电池电压 = 实测 × 2 */
#define BAT_SAMPLES       9                  /* 奇数，便于取中位数 */
#define BAT_PRESENT_MV    2500               /* 低于此值视为"未接电池" */
#define BAT_RAW_FULL_MV   3100               /* 未校准时的参考满量程 */
#define BAT_RAW_FULL_RAW  4095               /* 12bit */

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_cali;
static bool                      s_cali_ok;
static bool                      s_inited;

static int s_mv;                             /* 最近一次电池电压（mV） */
static int s_pct = -1;                       /* 0-100；-1 = 未接电池 */

/* 电压 → 电量百分比：锂电池放电曲线的分段线性近似（比纯线性更贴近实际显示） */
static int mv_to_pct(int mv)
{
    static const int tab[10][2] = {
        { 4200, 100 }, { 4100, 90 }, { 4000, 80 }, { 3900, 65 }, { 3800, 50 },
        { 3700, 35 },  { 3600, 20 }, { 3500, 10 }, { 3400, 5 },  { 3300, 0 }
    };
    if (mv >= tab[0][0]) return 100;
    if (mv <= tab[9][0]) return 0;
    for (int i = 0; i < 9; i++) {
        int v1 = tab[i][0], p1 = tab[i][1];
        int v0 = tab[i + 1][0], p0 = tab[i + 1][1];
        if (mv <= v1 && mv >= v0) {
            return p0 + (mv - v0) * (p1 - p0) / (v1 - v0);
        }
    }
    return 0;
}

void battery_init(void)
{
    if (s_inited) return;

    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = BAT_ADC_UNIT };
    if (adc_oneshot_new_unit(&ucfg, &s_adc) != ESP_OK) {
        ESP_LOGW(TAG, "adc_oneshot_new_unit failed -> battery display disabled");
        return;
    }
    adc_oneshot_chan_cfg_t ccfg = {
        .atten    = BAT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_oneshot_config_channel(s_adc, BAT_ADC_CHANNEL, &ccfg);

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cal = {
        .unit_id  = BAT_ADC_UNIT,
        .chan     = BAT_ADC_CHANNEL,
        .atten    = BAT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_cali_ok = (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) == ESP_OK);
#endif
    s_inited = true;
    ESP_LOGI(TAG, "ready: ADC1_CH8(GPIO9) atten=12dB cali=%d", (int)s_cali_ok);
}

void battery_poll(void)
{
    if (!s_inited) return;

    int raw[BAT_SAMPLES];
    int n = 0;
    for (int i = 0; i < BAT_SAMPLES; i++) {
        int r = 0;
        if (adc_oneshot_read(s_adc, BAT_ADC_CHANNEL, &r) == ESP_OK) raw[n++] = r;
    }
    if (n == 0) return;

    /* 插入排序取中位数（固定 9 个元素，不用动态内存） */
    for (int i = 1; i < n; i++) {
        int k = raw[i], j = i - 1;
        while (j >= 0 && raw[j] > k) { raw[j + 1] = raw[j]; j--; }
        raw[j + 1] = k;
    }
    int raw_mid = raw[n / 2];

    int mv = raw_mid * BAT_RAW_FULL_MV / BAT_RAW_FULL_RAW;   /* 粗略换算（兜底） */
    if (s_cali_ok) {
        int v = 0;
        if (adc_cali_raw_to_voltage(s_cali, raw_mid, &v) == ESP_OK) mv = v;
    }
    s_mv  = mv * BAT_DIVIDER;                                /* 还原电池真实电压 */
    s_pct = (s_mv >= BAT_PRESENT_MV) ? mv_to_pct(s_mv) : -1;
}

int  battery_mv(void)      { return s_mv; }
int  battery_pct(void)     { return s_pct; }
bool battery_present(void) { return s_mv >= BAT_PRESENT_MV; }
