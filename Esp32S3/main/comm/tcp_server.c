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

#define TCP_PORT 8899
static const char *TAG = "tcp_server";

/* 多连接共存：端口扫描/重连探测会临时建连，不应踢掉主连接。
 * s_active = 最近有数据到达的连接（用于回包与“是否在工作”判断）；s_conns = 当前连接数。 */
static volatile int s_active = -1;
static volatile int s_conns = 0;

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

/* 每个连接一个独立任务；连接之间互不影响（不再互相踢） */
static void client_task(void *arg)
{
    int sock = (int)(intptr_t)arg;
    frame_parser_t fp;
    frame_parser_init(&fp);
    uint8_t buf[512];

    for (;;) {
        int r = recv(sock, buf, sizeof(buf), 0);
        if (r <= 0) break;
        s_active = sock;                     /* 有数据 -> 记为活跃连接（回包/状态判断用它） */
        frame_parser_feed(&fp, buf, (size_t)r, on_line, &sock);
    }

    ESP_LOGI(TAG, "client %d disconnected (剩余 %d)", sock, (int)s_conns - 1);
    if (s_active == sock) s_active = -1;     /* 只有活跃连接断开才算链路中断 */
    s_conns--;
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
    ESP_LOGI(TAG, "TCP server listening :%d（多连接共存；端口扫描不再踢主连接）", TCP_PORT);

    for (;;) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof(ca);
        int c = accept(ls, (struct sockaddr *)&ca, &cl);
        if (c < 0) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }

        s_conns++;
        ESP_LOGI(TAG, "client connected: %s:%d（多连接共存，当前 %d 条）",
                 inet_ntoa(ca.sin_addr), ntohs(ca.sin_port), (int)s_conns);

        if (xTaskCreate(client_task, "tcp_cli", 8192, (void *)(intptr_t)c, 5, NULL) != pdPASS) {
            ESP_LOGE(TAG, "client task create fail");
            close(c);
            s_conns--;
        }
    }
}

void tcp_server_start(void)
{
    xTaskCreate(server_task, "tcp_srv", 4096, NULL, 5, NULL);
}

/* 是否有“活跃”连接（最近有数据到达）；端口扫描的临时连接不算 */
bool tcp_server_has_client(void)
{
    return s_active >= 0;
}
