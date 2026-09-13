#pragma once
/* M2：TCP Server :8899，按换行切帧（协议 V1.10 §0.1）；单主机（最新连接者接管）§0.5 */
void tcp_server_start(void);
