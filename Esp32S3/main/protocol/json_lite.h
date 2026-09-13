#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 极简 JSON 取值器（零依赖/无堆分配/不构建 DOM）。
 * 适用本项目扁平协议帧；局限：按 "key" 文本搜索取值，值内若出现同名键名可能误配（本协议不出现）。 */
bool jl_has_key(const char *json, const char *key);
bool jl_get_int(const char *json, const char *key, int def, int *out);
bool jl_get_str(const char *json, const char *key, char *out, int outsz);
/* 解析 "key": [[x,y],[x,y],...]；out_xy 交错存 x,y；返回点数 */
int  jl_get_pts16(const char *json, const char *key, int16_t *out_xy, int max);
