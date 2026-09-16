#pragma once
#include <stdint.h>

/* 协议消息分派（协议 V1.10 §3/§4）：
 * 收帧 -> 分派 -> 立即生效 + 回包（PONG / DEV_STATUS）。
 * 只做解析与业务动作，不关心承载（TCP 或 BLE 由调用方提供 send 回调）。 */

typedef void (*proto_send_fn)(const char *json_line, void *ctx);

/* 处理一行完整 JSON 报文（不含换行） */
void protocol_handle(const char *line, int len, proto_send_fn send, void *ctx);

/* JSON 解析失败/非法帧计数（DEV_STATUS 的 err 字段，协议 §4.1） */
uint32_t protocol_err_count(void);
void     protocol_err_reset(void);

/* 应用层链路阶段（纯报文驱动，判定依据唯一；不使用任何计时/计数附加条件）
 * 返回值只允许用下面两个具名常量比较，禁止写 stage < 3 这类魔法数字。 */
#define LINK_STAGE_WAIT_APP  1   /* 等待 App：5 秒内未收到任何报文 */
#define LINK_STAGE_CONNECTED 2   /* 已连接：有报文在流动 */
int  protocol_link_stage(void);
void protocol_link_reset(void);
