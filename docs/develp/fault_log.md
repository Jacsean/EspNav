# 故障档案（fault_log.md）

> 规范要求：遇错先检索本档案；按档案方案尝试 2 次未解决则跳出并追加新记录（不覆盖旧记录）。
> 每条含：现象 / 原因 / 解决方案 / 预防措施。

## F1. 屏幕黑屏（背光正常），面板 ID 读回全 0，fill 耗时 ~1000ms/色
- **现象**：背光亮、屏幕无内容；`fill RED done (1000+ ms)`（正常应 30-60ms）。
- **原因**：LCD 的 **CS 一直拉低**（`spics_io_num = -1` + 手动拉低）。在**完整初始化序列**（含 `0xF6` Interface Control，0x01 0x30）之后，面板不再接受"CS 恒低"的写入方式。
  - 反证：早期"极简 6 条命令"初始化时能显示（那时 CS 常低侥幸可用）；换完整序列后立刻黑屏。
- **解决方案**：CS 交由 **SPI 驱动逐笔控制**（`spi_device_interface_config_t.spics_io_num = LCD_PIN_CS`，不要手动常拉低）——与官方 esp_lcd 驱动一致。修复后 `fill done (43 ms)`，屏幕正常。
- **预防措施**：SPI 外设（含 CS/DC）一律交给外设驱动管理；如需手动控制，必须在完整初始化序列之后重新验证写入有效性。

## F2. 屏幕颜色反相（黑显示为白、黄显示为蓝）
- **现象**：填充黑→白、黄车标→蓝（互补色）。
- **原因**：**反相型面板**未发 `INVON`。
- **解决方案**：初始化序列加 `0x21`（INVON），或 esp_lcd 的 `esp_lcd_panel_invert_color(panel, true)`。留开关宏 `LCD_INVERT`。
- **预防措施**：点亮自检必须包含"整屏纯色轮换"，颜色异常一眼可辨。

## F3. 烧录失败（No serial data received）/ 环境混乱
- **原因**：① 应用在运行状态无法自动进入下载模式；② 机器上存在两套 IDF（git 克隆版 vs 安装器版），cmake 缓存记录的 Python 环境与实际激活的不一致（`MSys/Mingw is not supported`、python env 不匹配）。
- **解决方案**：① **按住 BOOT → 点一下 RST → 松开 BOOT** 手动进下载模式后立刻烧录；② 统一使用安装器版环境（"IDF PowerShell Environment"），必要时 `idf.py fullclean` 后重建；③ 项目路径与虚拟环境保持唯一。
- **预防措施**：烧录前先 `GetPortNames()` 确认端口；出现连接失败先手动进下载模式再排查硬件。

## F4. 编译错误：component 'json' could not be found
- **原因**：**IDF 6.1 已移除内置 cJSON（json 组件）**，迁移到组件管理器。
- **解决方案**：自研 `json_lite`（零依赖/无堆，按键取数字/字符串/坐标数组）；若确需 cJSON：`idf.py add-dependency "espressif/cjson"`。
- **预防措施**：不用外部 JSON 库解析自有协议；协议保持扁平结构以便极简解析。

## F5. 崩溃重启：A stack overflow in task tcp_srv
- **原因**：在 **TCP 接收任务里直接渲染整屏**（收帧即渲染），渲染/字库/格式化占用大量栈，超出任务栈（6KB）。
- **解决方案**：**渲染统一由显示任务完成**（收帧仅缓存，显示任务周期渲染）；TCP 任务栈提升到 12KB。
- **预防措施**：任务职责单一——通信任务不绘图；绘图只在显示任务；每新增重负载调用评估栈预算。

## F6. 源码写入损坏：`'
'` 变成真实换行导致编译错
- **原因**：脚本/管道写入 C 源码时，反斜杠转义被折叠，`'
'` 落盘成"引号+真实换行"。
- **解决方案**：改用 **ASCII 数字字面量**（`0x0A` / `0x0D` / `0x20`）替代字符转义；写盘后做"引号后跟换行"的扫描校验。
- **预防措施**：生成/批量修改 C 源码后，必须 git diff + 编译验证；避免在校验脚本里依赖反斜杠转义。

## F7. IDF 6.1 无内置 ILI9341 面板驱动
- **现象**：`esp_lcd_new_panel_ili9341` 在 IDF 内置 esp_lcd 中不存在。
- **原因**：ILI9341 驱动属组件管理器组件 `espressif/esp_lcd_ili9341`（官方 xiaozhi 固件即用它）。
- **解决方案**：当前采用**裸 SPI + 厂商完整初始化序列**（已验证可用）；如需官方同款：`idf.py add-dependency "espressif/esp_lcd_ili9341^2.0.2"`。
- **预防措施**：跨 IDF 版本移植时，先确认外设驱动是否内置（grep `components/`）。

## F8. 板级参数勿猜：以官方板级配置为准
- **现象**：曾按"同类板参考引脚"配置；虽引脚巧合一致，但色序/频率/初始化来源不明，反复试错。
- **原因**：`quandong-s3-dev` 是 xiaozhi 生态板，官方仓库已有板级配置。
- **解决方案**：以 `78/xiaozhi-esp32` → `main/boards/quandong-s3-dev/{config.h, quandong_s3_dev_board.cc}` 为权威来源（引脚、40MHz、MADCTL=0xA0、INVON、vendor 序列）。
- **预防措施**：拿到实物先找官方/厂商板级配置（开源生态常见），不要凭"同类板"猜测。

## F9. 编译失败：unknown type name 'gpt_t' / 一批 geo_* 函数未声明
- **现象**：M4 模板渲染加入后整包编译失败，报 `gpt_t` 未知、`geo_fatten`/`geo_proj_pt`/`geo_rbt_metrics`/`geo_azimuth_rad`/`geo_sweep_directed` 隐式声明。
- **原因**：`render_nav.c` **漏了 `#include "geo.h"`**（新增代码使用了 geo 模块的几何算法与类型，但未包含其头文件）；所有后续报错均为连锁反应。
- **解决方案**：补 `#include "geo.h"`。同时修复顺带发现的越界隐患：`fb_fill_poly` 交点缓冲 `xbuf[64]`、`road_fill` 坐标数组 `xs[2*NAV_MAX_PTS]`(=32) 均小于实际可能点数（geo_fatten 对 16 点输入输出可达 2*16+2=34 点 -> 多边形 68 点），已统一放大到 **128**。
- **预防措施**：新增代码后**先跑 `python Esp32S3/tools/precheck.py main`**（本档案新增的静态预检脚本，可直接报出"缺 include/类型不可见"），再交用户编译；同时按"代码质量门"做人工 review（重点是缓冲尺寸与点数上限）。

## F10. 编译失败：'s_blank_req' undeclared（使用先于声明）+ 预检缺口
- **现象**：`render_nav_clear()`（文件第 28 行）使用 `s_blank_req`，而该 static 变量定义在第 94 行 -> `error: 's_blank_req' undeclared`。
- **原因**：新增函数被插入到**文件前部**，但模块级 static 状态变量仍在文件中部（函数插入位置与变量定义顺序未同步检查）。更关键的是：**当时的预检脚本没有"使用先于声明"这一项**，因此漏检。
- **解决方案**：把模块级状态变量**统一提到文件顶部**（`s_cur/s_have/s_blank_req/s_anim`），从结构上杜绝此类错误；并在预检脚本中新增检查项 7（使用先于声明，覆盖 static 变量与文件内 static 函数）。
- **附带发现**：预检脚本自身的 `re.escape(name) + r''` 被写入工具折叠成了**退格符 0x08**，导致该检查静默失效（返回 0 问题）。已修复，并新增检查项 8（源码控制字符检测）；同时用**出错的历史版本**反证检查器有效。
- **预防措施**：① 模块级状态变量一律集中在文件顶部；② 新检查项上线后，必须用"曾经出错的历史版本"回归验证其有效性；③ 交付前除自动预检外，必须做**逐项人工 review**（声明顺序/系统头/数组边界/类型/栈/跨任务资源）。

## F11. 编译失败：`%s directive output may be truncated`（-Werror=format-truncation）
- **现象**：`wifi_sta.c` 三处报错——`snprintf((char *)wc.sta.ssid, sizeof(wc.sta.ssid), "%s", ssid)`（本地 `char ssid[64]` -> 驱动 `uint8_t ssid[32]`）；`snprintf(k, sizeof(k), "ssid%d", slot)`（`int` 范围推断最长 11 位 > `char k[12]` 可用空间）。
- **原因**：GCC 在**目标和源的长度都可静态推断**时会判定"可能截断"，而 ESP-IDF 默认把警告升级为错误（`-Werror`）。
- **解决方案**：① 写入驱动结构体一律用**显式长度 memcpy**（`size_t n = strlen(src); if (n > sizeof(dst)-1) n = sizeof(dst)-1; memcpy(dst, src, n);`，结构体已 `{0}` 初始化，尾部自然为 NUL）；② 索引/计数类参数改为 `uint8_t` 并用 `%u` 输出；③ 缓冲名加宽（`char k[16]`）。
- **预防措施**：① 每轮交付前 `grep -rn "snprintf(" main/` **逐条核对源与目标宽度**（已写入《代码质量门》）；② `tools/precheck.py` 新增检查项 9：目标与源都是本文件数组时**精确报错**，目标是结构成员（宽度不可知，如 `wc.sta.ssid`）时给**提示**要求人工核对；③ 这条无法完全自动化，必须保留人工核对环节。

## F12. 源码写入损坏（复发 5 次）：shell heredoc 折叠转义序列，字符串跨行未闭合
- **现象**：用户编译报 `error: missing terminating " character`，定位到 `protocol.c` 的 `HELLO_ACK` 字符串——源码里 `\n` 变成了**真实换行**，字符串因此跨行未闭合。
  同类历史：`re.escape(name) + r'\b'` 落盘成退格符 0x08（F10 附带发现）、C 字符串里的 `\n`/`\r` 落盘成真实换行（F6）。
- **原因**：**用 shell heredoc（`python - <<'EOF'`）写入含反斜杠转义的代码**时，转义序列在写入链路中被折叠。这些字符（换行、退格、制表）**在终端里不可见**，人工 review 时极易漏掉 —— 因此同一坑反复出现。
- **解决方案**：**写代码/脚本一律使用文件工具（write_file / edit_file）直接落盘**，不再经 shell 传递含转义的文本；必须用 shell 时，用 `chr(92)`/`chr(10)` 等**构造字符**而非书写转义序列。
- **预防措施**：① 修复后立即补静态检查（本次新增"**双引号字符串未闭合**"检查：逐行统计未转义双引号奇偶、跳过字符字面量，已用出错历史版本反证有效、确认 `softap_prov.c` 的 `'"'` 不误报）；② 任何"写入源码"的动作，落盘后必须跑 `tools/precheck.py`；③ 同类事故连续两次即视为**流程缺陷**（而非操作失误），必须新增自动化检查项。

