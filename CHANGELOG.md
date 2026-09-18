# 变更日志

本文件记录本项目的**重要变更**。
格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

---

## [未发布]

### 计划中（详见 [进度与路线图](docs/develp/进度与路线图.md)）
- 消息弹窗层（固件 UI 状态机，协议已定义 `POPUP_MSG`）
- 通知监听（微信/短信 → ESP 弹窗）
- BLE 备用承载 + mDNS 发现
- 百度导航 SDK 接入
- 航向传感器模块（替代高德 bearing）
- 实车道路测试 / 功耗测试

---

## [1.1.0] - 2026-09-18

> 169 个提交 / 132 文件 / +3.6 万行。从"能跑"到"能用"，并补齐调试手段。

### 新增

- **行程图采样重写**：新增 `PolylineSampler`
  - `simplifyVw`：Visvalingam 面积删点（面积 = ½·弦长·弧高 → 长直段先删、**长而缓的弧线自动保留**）+ **直角拐点锁定** + 首尾锁定（默认）
  - `simplifyDp`：Douglas-Peucker 自适应 ε（二分 ε 直到点数达标，显式栈防爆栈）
  - 设置页「行程图」组可**一键切换**两种算法
  - 米制换算 + `cos(纬度)` 折算，修正东西向被拉伸约 30% 的失真
- **行程预览页**（第 3 个 Tab）：内嵌 `sampling_compare.html`，同一条路径 VW/DP 并排对比（点数 / 最大偏差 px 与米 / 耗时），参数可调；同一份 HTML 在电脑浏览器也能打开
- **导航画面**：相机跟随车头（bearing=heading、45° 俯视）、已走灰/未走蓝双折线、车头黄色箭头、右下**行程图卡片**、顶部信息条与「结束导航」
- **独立配置文件**：`/sdcard/Download/EspNav/espnav_config.json`，卸载重装不丢；启动时自动检测并询问是否覆盖；设置页提供保存/恢复/删除（带确认）
- **导航仿真开关**：设置页可选「模拟行进 / GPS 实测」
- **ESP 显示元素调试开关**：道路图案 / 车道中线 / 行程图 / 车头三角 / 一键全关 —— 用于定位屏幕上的异常图元出自哪一层
- App 崩溃日志（`filesDir/crash.log`）、启动时显示

### 变更

- **行程图精度与观感**：`OV_SPAN` 40 → **200**（拐点分辨精度 ×5）；区域向左拓宽 3 个网格并**居中**；轨迹线宽 ×3、起终点与当前位置点 ×2
- **主视图数据源**：不再用"整条路线的 16 点降采样"过滤前方 400 m，改为在**全量路径**上按最近点索引沿路径顺序取点（点连续、顺序正确）
- 行程图黄点改用**当前定位直接投影**（不再按进度取数组索引，避免点距不均导致跳变）
- **APK 地图改用 `TextureMapView`**（原 `MapView` 基于 GLSurfaceView，会脱离 View 层级独立合成）
- 设置页文案与分组整理（连接 / 导航默认 / 地图 / 行程图 / ESP 显示元素 / 日志 / 权限 / 配置文件）

### 修复

- **车头附近随机折线**：根因是 `project()` 用 `coerceIn` 把越界点贴到屏幕边缘（每帧从车头连出一条贴边斜线）
- **预览「开始导航」按钮灰**：`btnStartNav.isEnabled` 只在 `updatePickState()` 里计算，而算路成功只设了 `visibility`
- **导航一次后再启动毫无反应**：`AMapNavi` 是进程级单例，`onInitNaviSuccess()` **只在首次初始化时回调**，第二次 `getInstance()` 不再触发 → 算路永不发起
- **地图选点后多出一个"意外终点"**：`onLocationChange` 的条件漏判 `fixedTo`，误走 `calculateAutoDest()`（当前位置向北 2 km）
- **最后路段道路图案旋转 / 消失**：
  - `quad_from_centerline` 只用首末点算一条法线 → 改用**逐点局部法线 + 沿路径收窄**（新增 `road_fill_screen` / `road_edges_screen`）
  - `ARRIVE`/`UTURN` 时 `roadOf()` 返回 null → 退回"按真实路径画路面" → 改为**也发固定模板**
  - 到达终点附近"前方"只剩 1 点 → 固件因 `center_n < 2` 整段不画 → App 端补足 ≥2 点 + 固件兜底
- **「行程预览」页一片漆黑**：`releaseCompare()` 用 `loadUrl("about:blank")` 释放，而 `ensureCompare()` 见 `compareView` 非空就直接 return → 切走再切回永远空白；改为真正销毁并置空 + 改用 `loadDataWithBaseURL` 注入 + 加可视状态行
- **APK 地图盖住连接页 / Tab 点不动 / 定位后全蓝**：`MapView`（GLSurfaceView）层级问题（同上，改 `TextureMapView`）
- **地图初始显示全城比例**：进导航页时先按默认视野显示，等定位回调才居中 → 改为缓存定位、创建后立即按 `defaultZoom` 居中
- `road_path()` 越界判断误用未投影坐标 → 改为按投影后坐标判断
- 高德「骑行途经点算路」为**收费接口**（调用不返回 → 表现为算路超时）→ 界面置灰并标注

### 移除

- **主视图「已走 / 未走路径」两条折线**（按用户要求，效果好不了；代码以 `if (0)` 保留便于恢复）
- 车头恒定竖线（暂停绘制，用户要求；恢复只需打开一行注释）
- 若干临时日志探针（改用颜色分离 + App 端开关排查）

---

## [1.0.01] - 2026-09-03

首个可运行基线（`ef49b6e base` 之后的整合）。

### 新增

- **固件**（ESP-IDF v6.1，板型 quandong-s3-dev）
  - LCD 驱动（ILI9341，320×240 SPI 40MHz）+ 帧缓冲与绘图原语
  - 渲染管线：透视路面 + 边界虚线动画 + 车道中线 + 车头三角
  - **7 类路况模板**：straight / curve / tjunc / cross / multi / roundabout / fork
  - **罗盘**（8 方位）+ **行程图**（网格 / 轨迹 / 起终点 / 当前位置黄点 / "北"指示）
  - 全 GB2312 点阵字库（6763 汉字 + ASCII）+ 文字滚动
  - 通信：softAP 配网页 + WiFi STA 多凭据自动重连 + TCP Server `:8899`
  - 协议：`\n` 切帧 + 极简 JSON 解析 + `HELLO`/`PING`/`GET_CONFIG`/`SET_CONFIG`/`CLEAR_SCREEN`/`NAV_FRAME`
- **App**（Android）
  - TCP 客户端（顺序发送 / 心跳 / 断线回调）
  - 协议层 `Protocol.kt`、模拟数据源 `MockNavigator`
  - 连接页控制界面（一键连接 / 扫描设备 / 配网页 / 日志区）
  - 高德骑行导航接入（`AmapNavSource`）
- **文档**：`ble_protocol.md`（协议 V1.10）、`模板几何规格.md`、实施方案 / 实施计划 / 技术实现路径 / 固件工程规划、`fault_log.md`
