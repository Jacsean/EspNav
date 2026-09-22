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

## F13. APK 启动即闪退：lateinit 属性在初始化之前被访问
- **现象**：安装新 APK 后一启动就闪退，无法进入主页面。
- **原因**：新增的 `refreshActionStates()` 被插入到 `onCreate` 中 `client = EspNavClient(...)` **之前**（第 62 行 vs 第 65 行），而该函数第一行就读 `client.isConnected`（`private lateinit var client`）→ `UninitializedPropertyAccessException`，启动即崩。
- **解决方案**：① 调用移到 `client` 初始化**之后**；② 函数内加守卫 `if (!::client.isInitialized) return`（顺序再被调乱也不会崩）。
- **预防措施**：① `onCreate` 中新增的刷新/初始化调用，一律放在**其依赖字段初始化之后**（推荐顺序：inflate → `savedBundle` → 崩溃捕获 → **依赖对象（client 等）** → 监听器 → **状态刷新**）；② 读取 `lateinit` 字段的辅助函数，首行加 `isInitialized` 守卫；③ **交付 APK 前必须真机启动一次**——仅靠静态预检覆盖不到"运行时初始化顺序"这一类问题，本次即为反例。

## F14. 回归 BUG：函数契约变更（3 态→2 态）漏改调用点，导致导航画面永不显示
- **现象**：串口里 `NAV_FRAME` 每 200ms 连续到达且解析完全正确（`hint/dist/road 模板`都对），网络、协议、解析全部正常，但**屏幕纹丝不动**，一直停在"等待导航数据"版式。用户质疑："多一个握手应答就卡住，逻辑上不成立"。→ **质疑成立**。
- **原因**：把 `protocol_link_stage()` 的返回值从 **1/2/3** 改成 **1/2**（2 态）时，**只改了函数本体，没改调用点**。`display_task` 里仍写着 `if (stage < 3) { 显示开机画面; continue; }` —— 由于函数现在**永远只返回 1 或 2**，该判断恒成立，`render_nav_tick()`（真正画导航画面的函数）**成了死代码**，从未被调用。
- **解决方案**：① `display_task` 改为与**具名常量**比较：`if (protocol_link_stage() == LINK_STAGE_WAIT_APP) {...}`；② 顺应用户要求删掉 `BOOT_MIN_S`/`nav_on`/`nav_t0`/`s_frame_count` 等非应答条件，画面切换**100% 由报文驱动**；③ 在 `protocol.h` 定义 `LINK_STAGE_WAIT_APP` / `LINK_STAGE_CONNECTED`，并在注释中**明令禁止**写 `stage < 3` 这类魔法数字。
- **预防措施**：① **修改函数返回值语义/取值范围后，必须 grep 全部调用点逐一核对**（`grep -rn "protocol_link_stage" main/`）；② 状态/枚举类返回值**一律用具名常量**，禁止魔法数字比较——编译器无法发现"3 态改 2 态漏改判断"，但具名常量能让这条判断在阅读时立刻暴露；③ 这类**语义回归静态预检抓不到**，只能靠"改契约必查调用点"的纪律 + 真机验证。





## F15. 编译失败：Kotlin 字符串跨行（补丁脚本把 `\n` 写成了真换行）

- **现象**：`e: MainActivity.kt:892:70 Expecting '"'`、`893:1 Expecting ')'`、`Unexpected tokens`。
- **原因**：生成代码的 Python 补丁脚本里写了 `"\n"`（Python 语义 = **真换行**），落盘到 Kotlin 源码后
  字符串字面量跨行 —— Kotlin 不允许字符串里直接换行。与 F12 同源（当时损坏的是 C 源码），本次是 Kotlin。
- **解决方案**：该处改为 `+ "\n\n是否…"`（Kotlin 转义）；即 Python 侧必须写 `\\n` 才能产出 `\n` 字面量。
- **预防措施**：① 凡"用脚本生成源码"，脚本里若要产出 `\n` 字面量，必须写成 `\\n`；
  ② 落盘后除了跑 `kotlin_precheck.py`，**还必须编译一次** —— 本次预检 0 问题却编译失败，
  说明预检覆盖不到"字符串跨行"这一类；③ 快速定位手段：`grep -n '+ "$' <file>`（行尾以 `+ "` 结尾即跨行字符串）。

## F16. 编译失败：CMakeLists 的 SRCS 条目误加逗号（CMake 把逗号当成独立源文件名）

- **现象**：`CMake Error at component.cmake:494 (add_library): Cannot find source file: .../Esp32S3/main/,` +
  `No SOURCES given to target: __idf_main` + `CMake Generate step failed`。
- **原因**：本项目 `CMakeLists.txt` 的 `SRCS` 是**空白分隔**列表（文件里所有条目都**没有逗号**）。
  新增 `"board/battery.c",` 时带了逗号 → CMake 解析出两个 token：`"board/battery.c"` 和独立的 `,`，
  后者被当成"相对路径的源文件名"。
- **解决方案**：去掉逗号，与文件内既有条目的风格保持一致。
- **预防措施**：① 往 `SRCS` 加条目时**照抄相邻条目的写法**（本项目无逗号，靠换行分隔）；
  ② 改完 CMake 相关文件后必须让 CMake **重新 configure 一次**验证（`ninja` 会自动触发，
  也可用 `idf.py reconfigure`）；③ CMake 报"找不到某文件，而那个'文件名'看着像标点"时，先查分隔符。

## F17. 自检通过但用户编译失败：`-fsyntax-only` 抓不到 `-Werror=format-truncation`

- **现象**：我自建的"用真实编译参数做语法检查"10 个文件全部 rc=0，但用户编译报
  `render_nav.c:741:33: error: '%d' directive output may be truncated writing between 1 and 9 bytes into a region of size 8 [-Werror=format-truncation=]`。
- **原因**：`-fsyntax-only` **只做语法分析，不做优化/格式串宽度分析**；`-Wformat-truncation` 属于
  优化阶段（`-O2`）的分析。`char buf[8]` + `"%d%%"`（编译器按 `int` 最坏情况算最多 11 字节）即触发。
- **解决方案**：`buf[8]` → `buf[16]`。
- **预防措施**：① 交付前自检**不能只做 `-fsyntax-only`**，必须**真正编译到 .o**
  （固件：`Esp32S3/tools/build_fw.ps1` → `ninja`；APK：`gradlew assembleDebug`）；
  ② `snprintf` 的目标缓冲按"最坏情况"给（整型至少 12 字节，`%u/%d` 一律别按当前取值范围算）。

## F18. native 崩溃：高德 SDK 被暂停后仍被调用（`runCatching` 抓不住）

- **现象**：导航中**锁屏**或**切到别的 App** → 立刻闪退；**一直导航到结束**（点「停止导航」）→ 也闪退。
  App 自带的 `crash.log` 为空。
- **原因**：`onPause` 时已执行 `mapView.onPause()`，但推流与相机跟随循环跑在 `lifecycleScope` 里
  （**`onPause` 不会取消它**）。循环继续调 `getMapScreenShot()` / `moveCamera()` / `addPolyline()` /
  `addMarker()` / `remove()` —— 对**已暂停的地图实例**调 API → 高德 **C++ 层崩溃（SIGSEGV）**。
  导航结束时则是因为 `endNav()` **顺序不当**（先 `stop()` 数据源、再动地图对象）+ 无防重入。
  **关键认知**：Kotlin 的 `try/catch` / `runCatching` 只能抓 Java 异常，**抓不到 native 崩**。
- **解决方案**：① 新增 `mapActive` 闸门（`onPause`/`onDestroy` 置 false，`onResume` 置 true）→
  推图、`updateNavUi`（相机/折线/标记）、`setNavUi(false)` 的 `remove()` **全部在调用前判断**；
  ② 截图回调到达时若已后台/已销毁 → 丢弃 bitmap；③ `endNav` 重排为"停循环 → 清图形 → 停数据源"，
  加防重入 `navEnding`，恢复视角改为延迟一拍 + `mapActive/isFinishing/isDestroyed` 三重守卫。
- **预防措施**：① **任何"后台仍在跑"的循环，碰 SDK 对象之前必须有显式可用性闸门**，不能只靠 `runCatching`；
  ② 生命周期成对调用（`onCreate/onResume/onPause/onDestroy`）后，要保证**同帧内没有其它路径**再调该 SDK；
  ③ native 崩溃**只能用 `adb logcat -b crash`** 定位（`crash.log` 看不到）。

## F19. `TextureMapView` 在 `visibility=GONE` 后停止渲染 → 切 Tab 时 ESP 底图停更

- **现象**：App 切到别的 Tab 后，ESP 端底图**停止更新**（导航文字信息照常刷新，因为那是定时发帧）。
- **原因**：`applyPage()` 切非导航 Tab 时执行了 `pageNav.visibility = GONE` + `mapView.onPause()`。
  `TextureMapView`（派生自 `TextureView`）**一旦 GONE 就停止渲染**，`getMapScreenShot()` 自然拿不到内容。
- **解决方案**：导航页**常驻 `VISIBLE`**，非导航 Tab 只把整页 `translationY` **移出可视区**
  （View 仍在 View 层级中、仍持有 Surface → **继续渲染**）；三处原本用
  `pageNav.visibility == VISIBLE` 判断"是否在导航页"的地方改用新状态变量 `curTab`
  （否则**返回键会永久失效**）。
- **预防措施**：① 用 `TextureMapView` 时牢记"**`GONE` = 停止渲染**"；需要"离开页面仍持续出图"
  就只能**移出可视区**（`translationX/Y`）或常驻可见，不能改可见性；
  ② 凡是把某控件的 `visibility` 当"当前页面"判据的代码，一律换成独立的状态变量（如 `curTab`），
  避免语义被后续改动破坏。

## F20. 闪退：发送队列无界（Channel.UNLIMITED）→ 导航数分钟后 OOM

- **现象**：导航中**保持在导航页不切换**，**过几个路口后**闪退（Java `crash.log` 里为内存类错误）。用户直觉怀疑"是 UI 改动弄坏了绑定"，但时间线即可排除：UI 绑定只在启动/进设置页时执行一次，有问题会立刻崩，不会等几分钟。
- **原因**：`EspNavClient` 的发送队列是 **`Channel<String>(Channel.UNLIMITED)`**，且 `queue()` 用 `trySend`（**永不阻塞、永不丢弃**）。导航时每 200ms 发一帧（JSON 含路况模版 / 行程图 / 中心线等较大字段），**只要 TCP 变慢**（信号差、热点抖动、对端 GC），生产者就永远快于消费者 → 队列无限增长 → **OOM**。属于**累积型**缺陷：跑得越久越危险，短测发现不了。
- **解决方案**：① 队列改**有界** `Channel<String>(capacity = 16)`（≈3.2 秒数据），满则**丢弃新帧**（对实时状态而言丢新≈丢旧）并累计 `droppedFrames` 计数便于诊断；② 顺带把聚线重建从"每 3 个路径点"节流为"**≥12 点且间隔 ≥1.5 秒**"，减少高德 native 侧反复 `addPolyline/remove`。
- **预防措施**：① 任何"生产快于消费"的通道**一律必须有界**，并明确满时是丢新还是丢旧；**`Channel.UNLIMITED` 只允许用在"消费者确定不会落后"的场合**；② 涉及"实时状态流"的代码，按**跑十几分钟**的标准评估内存增长，而不是"点几下不崩就行"；③ 这类崩溃**只在长时间运行后出现**，短测会漏 —— 真机验证要跑够时长。

## F21. 静态检查本身有盲区：漏扫 ViewBinding 形式，误判"控件未绑定"

- **现象**：为回应"UI 重排是否漏了控件处理"，我写脚本核对「XML 控件 ↔ 代码绑定」，结果报出 3 个控件（`seekBrightness`/`seekDashSpeed`/`switchAnim`）"只有壳没有逻辑" —— **结论是错的**。
- **原因**：脚本只扫了 `findViewById<T>(R.id.xxx)` 这一种形式，而项目里这三处用的是 **ViewBinding 路径**（`binding.pageSettings.seekBrightness`）。**检查工具的口径没覆盖全部引用形式**，导致漏判。
- **解决方案**：把 `binding.<page>.<id>` 也纳入扫描：105 个控件全部有引用（唯一"未引用"的是 `pageSettings` 本身，它是 `binding.pageSettings.root` 用的，正常）。
- **预防措施**：① 写静态检查时**先枚举项目里所有等价写法**（`findViewById` / ViewBinding / `findViewById` 无泛型 / `binding` 展开），再决定匹配规则；② **检查工具报"缺失"时，先怀疑工具口径**，用 `grep` 交叉验证一遍再下结论；③ 这类"工具盲区"会造成**错误的自信**（比不检查更危险），因此结论要标注"由静态推断"，并留真机/日志验证的出口。

