#pragma once
#include <stdbool.h>
#include <stdint.h>

/* ================= 【M1.5 静态图导航】地图截图通道 =================
 * 链路：App 截屏 → 压暗/缩放 → JPEG → base64 分块 → IMG_BEGIN/IMG_CHUNK/IMG_END
 *       → 固件累积字节 → ROM tjpgd(jd_prepare/jd_decomp) 解码 → 底图副本 + framebuffer。
 *
 * 线程约定（见 fault_log F5：只有 display_task 能碰 framebuffer）：
 *   · 接收侧（TCP 任务 / protocol.c）只做"base64 解码 + 累积到静态 JPEG 缓冲 + 置标志"；
 *   · 解码与贴图在渲染侧（display_task）执行。
 *
 * 约定：App 负责把图处理成 **正好 320×240（全屏）或 320×160（上半）** 的 JPEG
 *       （q70 约 12–25KB），固件不做缩放，只按声明尺寸贴到 (x,y)。
 */
#define MAP_IMG_MAX_BYTES 32768   /* JPEG 原始字节上限（协议单帧 ≤2048B → base64 块 ≤1720 字符） */
#define MAP_IMG_B64_MAX   1720    /* 单块 base64 字符串缓冲（含结尾 NUL） */

/* --- 接收侧（protocol.c / TCP 任务调用；不碰 framebuffer）--- */
void map_image_begin(int seq, int w, int h, int x, int y, int bytes);
bool map_image_chunk(int seq, const char *b64);
bool map_image_end(int seq);

/* --- 渲染侧（draw_frame 内调用）--- */
bool map_image_has(void);      /* 是否已有可用底图 */
void map_image_apply(void);    /* 有待解码图 → 解码并写入底图副本（幂等；无待解码图时直接返回）*/
void map_image_restore(void);  /* 把底图副本整屏拷进 framebuffer（每帧开始时调用）*/
void map_image_clear(void);    /* 丢弃底图（回到模板路面模式；调试/退出导航用）*/
