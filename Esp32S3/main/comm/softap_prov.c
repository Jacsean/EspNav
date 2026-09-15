#include "softap_prov.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "wifi_sta.h"

static const char *TAG = "prov";

#define SCAN_MAX 15

static httpd_handle_t s_srv = NULL;
static char           s_json[1600];     /* 扫描结果 JSON（httpd 单任务，静态缓冲足够） */

/* ---------------- HTTP 小工具 ---------------- */

static void url_decode(char *s)
{
    char *o = s;
    while (*s) {
        if (*s == '+') { *o++ = ' '; s++; }
        else if (*s == '%' && s[1] && s[2]) {
            char hex[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else {
            *o++ = *s++;
        }
    }
    *o = 0;
}

/* 从 "ssid=abc&pass=xyz" 取字段 */
static void form_get(const char *body, const char *key, char *out, size_t out_sz)
{
    char pat[24];
    snprintf(pat, sizeof(pat), "%s=", key);
    const char *p = strstr(body, pat);
    out[0] = 0;
    if (!p) return;
    p += strlen(pat);
    size_t i = 0;
    while (*p && *p != '&' && i < out_sz - 1) out[i++] = *p++;
    out[i] = 0;
}

/* JSON 字符串转义（仅处理引号与反斜杠，够用） */
static void json_escape(const char *in, char *out, size_t out_sz)
{
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 2 < out_sz; i++) {
        if (in[i] == '"' || in[i] == '\\') out[j++] = '\\';
        out[j++] = in[i];
    }
    out[j] = 0;
}

/* ---------------- 页面 ---------------- */

static const char *HTML_INDEX =
"<!doctype html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>EspNav 配网</title><style>"
"body{font-family:sans-serif;margin:16px;background:#111;color:#eee}"
"input,button{font-size:16px;padding:10px;width:100%;box-sizing:border-box;margin:6px 0;"
"background:#222;color:#eee;border:1px solid #444;border-radius:6px}"
"button{background:#1b5e20;border:none}"
".s{color:#9e9e9e;font-size:13px}"
"ul{list-style:none;padding:0}li{padding:8px;border-bottom:1px solid #333}"
"a{color:#4caf50}"
"</style></head><body><h2>EspNav 配网</h2>"
"<p class='s'>填入手机热点或家里路由器的 Wi-Fi（最多保存 3 组，开机自动连接）。"
"保存后 ESP32 会主动连上该网络，手机即可保持上网。</p>"
"<button onclick='scan()'>搜索附近 WiFi</button><ul id='list'></ul>"
"<form method='post' action='/save'>"
"<label class='s'>WiFi 名称 (SSID)</label>"
"<input id='ssid' name='ssid' autocomplete='off' placeholder='例如 iPhone 热点名'>"
"<label class='s'>密码</label><input id='pass' name='pass' type='password'>"
"<button type='submit'>保存并连接</button></form>"
"<p><a href='/status'>查看设备状态 (JSON)</a></p>"
"<script>"
"function scan(){"
"fetch('/scan').then(r=>r.json()).then(a=>{"
"var u=document.getElementById('list');u.innerHTML='';"
"a.forEach(x=>{var li=document.createElement('li');"
"li.textContent=x.ssid+'  ('+x.rssi+' dBm)';"
"li.onclick=function(){document.getElementById('ssid').value=x.ssid;};"
"u.appendChild(li);});"
"}).catch(function(){alert('扫描失败，请重试');});"
"}"
"</script></body></html>";

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, HTML_INDEX, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t save_post(httpd_req_t *req)
{
    char body[256];
    char ssid[64] = "";
    char pass[64] = "";
    char page[640];

    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
        return ESP_FAIL;
    }
    body[len] = 0;
    form_get(body, "ssid", ssid, sizeof(ssid));
    form_get(body, "pass", pass, sizeof(pass));
    url_decode(ssid);
    url_decode(pass);

    bool ok = wifi_sta_save_credential(ssid, pass);
    ESP_LOGI(TAG, "配网提交: ssid=%s -> %s", ssid, ok ? "已保存" : "失败");

    snprintf(page, sizeof(page),
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<meta http-equiv='refresh' content='5;url=/'>"
        "<title>已保存</title></head>"
        "<body style='background:#111;color:#eee;font-family:sans-serif;margin:16px'>"
        "<h2>%s</h2><p>SSID：%s</p>"
        "<p class='s' style='color:#9e9e9e'>设备将在约 10 秒内尝试连接该网络；"
        "连上后请在 <a href='/status' style='color:#4caf50'>状态页</a> 查看 IP。</p>"
        "</body></html>", ok ? "已保存 ✓" : "保存失败 ✗", ssid);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t scan_get(httpd_req_t *req)
{
    wifi_scan_config_t cfg = { 0 };
    esp_err_t r = esp_wifi_scan_start(&cfg, true);   /* 阻塞扫描（约 2s） */
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "scan_start -> %s", esp_err_to_name(r));
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "[]", HTTPD_RESP_USE_STRLEN);
    }

    uint16_t n = SCAN_MAX;
    wifi_ap_record_t recs[SCAN_MAX];
    memset(recs, 0, sizeof(recs));
    if (esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) n = 0;

    size_t off = 0;
    off += (size_t)snprintf(s_json + off, sizeof(s_json) - off, "[");
    for (uint16_t i = 0; i < n && off < sizeof(s_json) - 80; i++) {
        char e[80];
        json_escape((const char *)recs[i].ssid, e, sizeof(e));
        off += (size_t)snprintf(s_json + off, sizeof(s_json) - off, "%s{\"ssid\":\"%s\",\"rssi\":%d}",
                                (i ? "," : ""), e, recs[i].rssi);
    }
    snprintf(s_json + off, sizeof(s_json) - off, "]");
    ESP_LOGI(TAG, "扫描完成，发现 %u 个热点", (unsigned)n);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, s_json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_get(httpd_req_t *req)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"sta_connected\":%s,\"ip\":\"%s\",\"rssi\":%d,\"saved\":%d,\"ap_ssid\":\"ESPNav-AP\",\"port\":8899}",
             wifi_sta_is_connected() ? "true" : "false",
             wifi_sta_ip_str(), wifi_sta_rssi(), wifi_sta_cred_count());
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

/* ---------------- 启动 ---------------- */

void softap_prov_init(void)
{
    if (s_srv) return;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;          /* 扫描 + 字符串拼接留足栈 */
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 8;

    if (httpd_start(&s_srv, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start 失败，配网页不可用");
        return;
    }

    httpd_uri_t u_index  = { .uri = "/",       .method = HTTP_GET,  .handler = index_get  };
    httpd_uri_t u_save   = { .uri = "/save",   .method = HTTP_POST, .handler = save_post  };
    httpd_uri_t u_scan   = { .uri = "/scan",   .method = HTTP_GET,  .handler = scan_get   };
    httpd_uri_t u_status = { .uri = "/status", .method = HTTP_GET,  .handler = status_get };
    httpd_register_uri_handler(s_srv, &u_index);
    httpd_register_uri_handler(s_srv, &u_save);
    httpd_register_uri_handler(s_srv, &u_scan);
    httpd_register_uri_handler(s_srv, &u_status);

    ESP_LOGI(TAG, "配网页就绪: 手机连 ESPNav-AP 后访问 http://192.168.4.1");
}
