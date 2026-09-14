#include "tcp_server.h"
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "frame_parser.h"
#include "nav_frame.h"

#define TCP_PORT 8899
static const char *TAG = "tcp_server";

static void on_line(const char *line, size_t len, void *ctx)
{
    (void)ctx;
    nav_frame_on_json_line(line, (int)len);
}

static void handle_client(int sock)
{
    frame_parser_t fp;
    frame_parser_init(&fp);
    uint8_t buf[512];
    for (;;) {
        int r = recv(sock, buf, sizeof(buf), 0);
        if (r <= 0) break;
        frame_parser_feed(&fp, buf, (size_t)r, on_line, NULL);
    }
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
    listen(ls, 1);
    ESP_LOGI(TAG, "TCP server listening :%d（最新连接者接管，协议 V1.10 §0.5）", TCP_PORT);

    for (;;) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof(ca);
        int c = accept(ls, (struct sockaddr *)&ca, &cl);
        if (c < 0) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        ESP_LOGI(TAG, "client connected: %s:%d", inet_ntoa(ca.sin_addr), ntohs(ca.sin_port));
        handle_client(c);
        close(c);
        ESP_LOGI(TAG, "client disconnected");
    }
}

void tcp_server_start(void)
{
    xTaskCreate(server_task, "tcp_srv", 12288, NULL, 5, NULL);
}
