#include "tcp_server.h"
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "frame_parser.h"
#include "protocol.h"
#include "render_nav.h"

#define TCP_PORT 8899
static const char *TAG = "tcp_server";

/* 当前主机（单主机：最新连接接管，协议 V1.10 §0.5） */
static volatile int s_client = -1;

/* 回包：写入当前客户端 socket（json_line 已含换行） */
static void send_json(const char *json_line, void *ctx)
{
    int sock = *(int *)ctx;
    if (sock >= 0 && json_line) {
        int n = send(sock, json_line, strlen(json_line), 0);
        if (n < 0) ESP_LOGW(TAG, "send fail errno=%d", errno);
    }
}

static void on_line(const char *line, size_t len, void *ctx)
{
    protocol_handle(line, (int)len, send_json, ctx);
}

/* 每个连接一个任务；被接管时旧任务因 socket 关闭而自然退出 */
static void client_task(void *arg)
{
    int sock = (int)(intptr_t)arg;
    frame_parser_t fp;
    frame_parser_init(&fp);
    uint8_t buf[512];

    for (;;) {
        int r = recv(sock, buf, sizeof(buf), 0);
        if (r <= 0) break;
        frame_parser_feed(&fp, buf, (size_t)r, on_line, &sock);
    }

    ESP_LOGI(TAG, "client %d disconnected -> 显示“信号中断”提示（保留最后画面）", sock);
    render_nav_link_lost();
    if (s_client == sock) s_client = -1;
    close(sock);
    vTaskDelete(NULL);
}

static void server_task(void *arg)
{
    (void)arg;
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0) { ESP_LOGE(TAG, "socket fail errno=%d", errno); vTaskDelete(NULL); return; }

    int opt = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(TCP_PORT);

    if (bind(ls, (struct sockaddr *)&a, sizeof(a)) != 0) {
        ESP_LOGE(TAG, "bind fail errno=%d", errno);
        close(ls);
        vTaskDelete(NULL);
        return;
    }
    listen(ls, 2);
    ESP_LOGI(TAG, "TCP server listening :%d（单主机，最新连接接管）", TCP_PORT);

    for (;;) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof(ca);
        int c = accept(ls, (struct sockaddr *)&ca, &cl);
        if (c < 0) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }

        int old = s_client;
        if (old >= 0 && old != c) {
            ESP_LOGW(TAG, "新连接接管：关闭旧连接 %d", old);
            s_client = -1;
            shutdown(old, SHUT_RDWR);          /* 由旧任务负责 close */
        }
        s_client = c;
        ESP_LOGI(TAG, "client connected: %s:%d（接管）", inet_ntoa(ca.sin_addr), ntohs(ca.sin_port));

        if (xTaskCreate(client_task, "tcp_cli", 8192, (void *)(intptr_t)c, 5, NULL) != pdPASS) {
            ESP_LOGE(TAG, "client task create fail");
            close(c);
            s_client = -1;
        }
    }
}

void tcp_server_start(void)
{
    xTaskCreate(server_task, "tcp_srv", 4096, NULL, 5, NULL);
}

bool tcp_server_has_client(void)
{
    return s_client >= 0;
}
