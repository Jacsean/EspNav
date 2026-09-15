# EspNav 控制器（Android，P1 最小可用）

手机端 App：通过 WiFi-TCP 把导航帧发给 ESP32-S3 外置导航屏；本阶段（P1）用**模拟导航数据**打通
「手机 → ESP32 → 320×240 屏幕」整条链路，后续 P3 再接入高德导航 SDK 作为真实数据源。

## 环境要求
- Android Studio（任意近三年版本；本项目用 **AGP 7.4.2 / Gradle 7.5 / Kotlin 1.8.10**，兼容性优先）
- JDK 11+（Android Studio 自带）
- 手机 Android 7.0+（minSdk 24）；本项目在华为 Mate 40 Pro / HarmonyOS 4.0 上联调
- 首次 Sync 需要联网下载 Gradle 与依赖

## 导入与编译
1. Android Studio → **Open** → 选择本目录 `mobileApp/android`（不要选上层目录）
2. 等待 **Gradle Sync** 完成（首次会下载 Gradle 7.5 与依赖库）
3. 手机开启「开发者选项 → USB 调试」，USB 连接后点 **Run ▶**
4. 若 IDE 提示 Gradle/AGP 需要升级：改 `build.gradle` 里的 `com.android.application` 版本号
   （与 `app/build.gradle` 的 `compileSdk` 一起升，例如 7.4.2 → 8.1.2 / compileSdk 34），再 Sync

## 联调步骤（真机）
1. 给 ESP32 上电（固件已就绪），日志应出现 `AP_START: 热点就绪 -> SSID=ESPNav-AP`
2. 手机 **WiFi 连接热点 `ESPNav-AP`**（密码 `espnav1234`）
   - 注意：HarmonyOS 可能提示「此网络无法上网」→ 选择**保持连接**
3. 打开 App → 地址默认 `192.168.4.1`、端口 `8899` → 点 **连接**
   - 连接成功后会立刻自动发一次 `GET_CONFIG`，日志区显示 `← DEV_STATUS 亮度=80 速度=40 ...`
4. 点 **开始模拟导航** → 手机每 200ms 发一帧 NAV_FRAME（5 Hz）
   - 屏幕应依次显示：直行 → 弯道左 → T 口左转 → 十字右转 → 环岛第 2 出口 → 多岔 → 匝道右（循环）
   - 罗盘方位随阶段变化、行程图黄点逐步前移
5. 其他按钮：
   - **发送一帧**：只发一帧（阶梯式调试）
   - **PING / 读配置**：验证回包（`← PONG` / `← DEV_STATUS`）
   - **清屏**：屏幕进入黑屏待机
   - **背光亮度 / 虚线速度**：松开滑杆时发送 `SET_CONFIG`，屏幕立即变化
   - **流动动画**：关闭后虚线静止
6. 断开连接（或退出 App）→ 屏幕自动清屏（固件协议 §6 行为）

## 目录结构
```
mobileApp/android/
├── settings.gradle / build.gradle / gradle.properties   # 工程与插件版本
└── app/
    ├── build.gradle                                     # 模块配置（viewBinding、依赖）
    └── src/main/
        ├── AndroidManifest.xml                          # INTERNET / ACCESS_NETWORK_STATE
        ├── java/com/espnav/app/
        │   ├── MainActivity.kt                          # 控制界面（连接/发帧/配置/日志）
        │   ├── protocol/Protocol.kt                     # 协议层：NavFrame/RoadSpec/OutMsg/InMsg
        │   ├── net/EspNavClient.kt                      # TCP 客户端（顺序发送/心跳/断线回调）
        │   └── data/MockNavigator.kt                    # P1 模拟导航数据源（P3 由高德替换）
        └── res/layout/activity_main.xml                 # 界面布局（XML）
```

## 协议要点（与固件 V1.10 一致）
- 传输：TCP `:8899`，**一行一条 JSON**（UTF-8，`\n` 结尾）；单主机，最新连接接管
- 出站：`NAV_FRAME`（渲染帧）、`SET_CONFIG`、`GET_CONFIG`、`CLEAR_SCREEN`、`PING`
- 入站：`DEV_STATUS`（配置/版本/err，SET_CONFIG 也会回一条作为确认）、`PONG`
- 帧率上限 10fps；本 App 默认 5fps（200ms）
- 坐标约定：`centerLine/overview` 等为**屏幕像素坐标**（320×240，原点左上），路面近端 y=144、车标 y=110

## 常见问题
| 现象 | 处理 |
|---|---|
| 连接超时 | 确认手机连的是 `ESPNav-AP`；固件日志出现 `TCP server listening :8899`；关掉手机「智能切换网络/WLAN+」 |
| 连上但屏幕不动 | 点「开始模拟导航」；或先「发送一帧」确认链路；看日志是否有 `← DEV_STATUS` |
| Sync 失败 | 检查网络/代理；必要时 File → Invalidate Caches and Restart |
| 提示缺 gradle-wrapper.jar | Android Studio 一般会自动补齐；或命令行执行 `gradle wrapper --gradle-version 7.5` |

## 方式 B：不做 USB 调试，直接装 APK（推荐用于日常联调）
App 与 ESP32 之间是 **WiFi-TCP**，**不依赖 USB**，所以手动装 APK 完全够用：

1. Android Studio 菜单 **Build → Build Bundle(s) / APK(s) → Build APK(s)**
2. 等右下角出现 “APK(s) generated successfully” → 点 **locate**
   - 产物路径：`mobileApp/android/app/build/outputs/apk/debug/app-debug.apk`
3. 把 APK 传到手机（数据线 / 微信文件传输助手 / 局域网）
4. 手机上打开该文件安装（首次需允许「安装未知应用」）
5. 打开 App → 按上面“联调步骤”操作

## Run 没反应 / 手机不被识别：排查顺序
1. **看设备下拉框**：Android Studio 顶部工具栏中间的设备下拉是否有手机型号？
   - 为空 = 设备未识别（继续第 2 步）；有型号 = 选中它再点 ▶
2. **命令行确认 adb 是否识别**（PowerShell）：
   ```powershell
   & "$env:LOCALAPPDATA\Android\Sdk\platform-toolsdb.exe" kill-server
   & "$env:LOCALAPPDATA\Android\Sdk\platform-toolsdb.exe" start-server
   & "$env:LOCALAPPDATA\Android\Sdk\platform-toolsdb.exe" devices
   ```
   - 输出为空 → USB 驱动/线材/模式问题（见第 3 步）
   - 显示 `unauthorized` → 看手机屏幕，点「允许 USB 调试」并勾选「一律允许」
   - 显示 `device` → 设备正常，问题在 AS：**Run → Edit Configurations**，确认有 `app` 配置、Module 选 `app`
3. **手机侧设置（华为/HarmonyOS 常见坑）**：
   - USB 连接方式改为 **“传输文件”**（不要选“仅充电”）
   - 开发者选项里打开 **“仅充电模式下允许 ADB 调试”**
   - 部分华为机型还需开启 **“USB 调试（安全设置）”**
   - 换一根**数据线**（很多线只能充电不能传数据）与另一个 USB 口
4. 若仍不识别：直接用**方式 B（构建 APK 手动安装）**，不必纠缠 USB 调试。
