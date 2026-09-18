# EspNav · ESP32-S3 外置导航屏

把手机导航的关键信息投到一块 **2.8″ / 320×240** 的 ESP32-S3 小屏上 —— 骑行时不用掏手机、也不用听语音。

**当前版本：[v1.1.0](CHANGELOG.md)** ｜ 固件 `V1.1.0` ｜ App `1.1.0`

---

## 1. 它是什么

| 组成 | 职责 |
| :--- | :--- |
| **手机 App**（Android） | 跑高德骑行导航 → 把「下一步动作 / 距离 / 航向 / 行程统计 / 行程缩略图」压成 JSON 帧下发 |
| **通信链路** | **WiFi-TCP**（ESP32 作 TCP Server `:8899`，手机作 Client）；首次配网经 ESP32 的 softAP 网页；BLE-GATT 为**规划中**的备用承载 |
| **ESP32 屏** | 收到帧后渲染：透视路面 + 罗盘 + 行程图 + 提示文字 + 行程统计 |

**关键分工**：导航计算、地图、路径**全部在手机端**；ESP32 只做渲染。
所以换手机、换导航 SDK（高德/百度）都不需要改屏幕端 —— 协议是唯一的契约。

```
手机 App ──WiFi-TCP(JSON, 按 \n 分帧)──> ESP32-S3 ──SPI──> ILI9341 320×240
  AmapNavSource → NavState → NavFrame                     render_nav.c → fb
```

---

## 2. 快速上手（4 步）

### 2.0 需要准备

| 项 | 说明 |
| :--- | :--- |
| 硬件 | **quandong-s3-dev**（ESP32-S3 + 2.8″ ILI9341 320×240 SPI + FT6336G 触摸），实测可用 |
| 电脑 | Windows + **ESP-IDF v6.1**（本项目基线，不降级） |
| 手机 | Android；本项目在 **Huawei Mate 40 Pro / HarmonyOS 4** 上实测通过 |
| 高德 Key | 必须开通 **「Android 导航 SDK」**（只开通地图 SDK 会算路失败），包名 `com.espnav.app` + 对应 SHA1 |

### 2.1 烧固件

```bash
cd Esp32S3
idf.py -p COM3 build flash monitor
```

开机画面应显示 **`EspNav v1.1`**。

### 2.2 装 App

```bash
cd mobileApp/android
export JAVA_HOME="$HOME/.jdks/jbr-17.0.14"     # Gradle 7.6.6 不兼容 JDK 25
./gradlew --no-daemon :app:assembleDebug
```

产物：`mobileApp/android/app/build/outputs/apk/debug/app-debug.apk`
（仓库里也放了一份可直接安装的：`mobileApp/android/espnav-debug.apk`）

### 2.3 连接

1. 手机连上 ESP32 的热点 **`ESPNav-AP`**
2. 打开 App →「**一键连接**」（会自动发现 `192.168.4.1`）
3. 要用**真实导航**（需要外网）时：浏览器打开 `http://192.168.4.1`，把 ESP32 配到**你手机的热点**，再让手机切回该热点并「一键连接」

### 2.4 导航

App 里输入起终点（或地图选点）→「**预览路线**」→「**开始导航**」。
ESP 屏随即显示路面、罗盘、行程图；App 地图进入导航画面（相机跟随、已走灰/未走蓝）。

> 室内测试：设置 → 打开「**导航仿真（模拟行进）**」，不用真骑车也能看效果；
> **上路实测前记得关掉它**，否则不会跟你的真实位置。

详细安装、配网与常见问题见 → [docs/public/用户手册.md](docs/public/用户手册.md)

---

## 3. 目录结构

```
Esp32S3/                      固件（ESP-IDF v6.1）
├── main/app_main.c           启动序列：AP → STA → 配网 → mDNS → TCP
├── main/display_task.c       显示任务（33ms tick，唯一访问 SPI/帧缓冲的任务）
├── main/comm/                wifi_sta / wifi_ap / softap_prov / tcp_server / mdns_service / ble_fallback
├── main/protocol/            frame_parser(按 \n 切帧) / json_lite / nav_frame / protocol(消息分派) / config
├── main/render/              render_nav(主渲染) / geo(模板几何+投影) / lcd_fb(帧缓冲)
├── main/font/                16×16 点阵字库（全 GB2312 + ASCII）+ 绘制
├── main/lcd/                 lcd_driver / ili9341
└── tools/precheck.py         固件静态预检

mobileApp/android/            Android App
└── app/src/main/java/com/espnav/app/
    ├── MainActivity.kt        单 Activity：连接页 / 导航页 / 行程预览页 + 设置
    ├── NavService.kt          前台服务（熄屏保活）
    ├── protocol/Protocol.kt   协议帧定义与 JSON 组装
    ├── net/EspNavClient.kt    TCP 客户端（顺序发送 / 心跳 / 断线回调）
    ├── data/                  NavSource 抽象 / AmapNavSource(高德) / NavState / NavStateMapper
    │                          PolylineSampler(行程图采样 VW+DP) / AppPrefs / ConfigFile / MockNavigator
    ├── ui/TripOverviewView.kt 导航画面右下角的行程图卡片
    └── assets/sampling_compare.html   行程预览页（VW/DP 并排对比，WebView 加载）
    └── tools/kotlin_precheck.py        App 静态预检

docs/
├── public/                   对外文档：用户手册、连接与联调手册
└── develp/                   开发文档：进度与路线图 / 开发指南 / 架构与数据流 /
                              协议(ble_protocol.md) / 模板几何规格 / 故障档案 / 备份
```

---

## 4. 文档地图

| 我想… | 打开 |
| :--- | :--- |
| **过段时间回来接着做** | **[docs/develp/进度与路线图.md](docs/develp/进度与路线图.md)** ← 先看这份 |
| 装 / 用这套系统 | [docs/public/用户手册.md](docs/public/用户手册.md) |
| 改代码（环境、编译、调试） | [docs/develp/开发指南.md](docs/develp/开发指南.md) |
| 搞懂系统怎么搭的、数据怎么流 | [docs/develp/架构与数据流.md](docs/develp/架构与数据流.md) |
| 看报文格式 | [docs/develp/ble_protocol.md](docs/develp/ble_protocol.md) |
| 看模板几何/投影规格 | [docs/develp/模板几何规格.md](docs/develp/模板几何规格.md) |
| 看踩过的坑（**改代码前扫一眼**） | [docs/develp/fault_log.md](docs/develp/fault_log.md) |
| 看这版改了什么 | [CHANGELOG.md](CHANGELOG.md) |
| 一次性联调（连接/配网/回包） | [docs/public/连接与联调手册.md](docs/public/连接与联调手册.md) |

---

## 5. 常用命令

| 用途 | 命令 |
| :--- | :--- |
| 固件静态预检 | `python Esp32S3/tools/precheck.py main` |
| App 静态预检 | `python mobileApp/android/tools/kotlin_precheck.py mobileApp/android` |
| 编译 App | `cd mobileApp/android && JAVA_HOME=~/.jdks/jbr-17.0.14 ./gradlew --no-daemon :app:assembleDebug` |
| 烧固件 | `cd Esp32S3 && idf.py -p COM3 build flash monitor` |
| 页面语法自检 | `python docs/develp/check_preview.py docs/develp/nav_compass.html` |

> ⚠️ **改代码后必须**：跑对应静态预检（要求 `problems: 0`）→ 编译一次 → 编译错误记进 `fault_log.md`。
> 完整「代码质量门」见 [开发指南](docs/develp/开发指南.md#代码质量门)。

---

## 6. 当前状态

| 层 | 状态 |
| :--- | :--- |
| 固件 M1–M6 | ✅ 全部真机验证通过（屏幕点亮 / 渲染管线 / 罗盘+行程图 / 通信协议 / 模板 7 类 / 正式承载） |
| 固件 M7 弹窗层 | ⬜ **未做**（协议已定义 `POPUP_MSG`，固件暂忽略） |
| App | ✅ 高德骑行导航全流程可用；v1.1.0 新增导航画面 + 独立配置文件 + 导航仿真开关 |
| 未做 | 消息弹窗层、通知监听、BLE 备用、百度 SDK、航向传感器、实车/功耗测试 |

完整清单与入手点 → **[进度与路线图](docs/develp/进度与路线图.md)**
