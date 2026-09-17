# 行程图折线采样与「行程预览」方案

## 变更日志

| 日期 | 修改人 | 原因 | 关联版本 |
| :--- | :--- | :--- | :--- |
| 2026-09-17 | Reasonix | 新建：行程图采样（VW/DP 可切换）+ 行程预览 Tab + 三项失真修正 | v1.0.0.1 |
| 2026-09-17 | Reasonix | 实施完成（提交 a2ad10f）：预检 0 问题 / BUILD SUCCESSFUL / 算法级测试全通过；真机效果待用户验证 | v1.0.0.1 |

---

## 1. 背景与问题（用户实测 + 代码定位）

ESP32 屏右上角行程图（小地图）的路径形状**拟真度不理想**，用户的观察是：
"明显的拐弯前后都有较长的直行路段，行程图不能准确表示"。

代码定位（改动前）：

| 步 | 位置 | 做法 | 问题 |
| :--- | :--- | :--- | :--- |
| 1 | `AmapNavSource.kt:236` | 高德 `getCoordList()` 全量路径（500~2000 点）→ `pathAllCoords` | — |
| 2 | `AmapNavSource.kt:237` | `pathCoords = downsample(pathAllCoords, 16)` | **等间隔取样，不看形状** |
| 3 | `AmapNavSource.kt:316` | 行程图 = `miniMapFromGeo(pathCoords)` | 只有 16 点 |
| 4 | `NavStateMapper.kt` | 包围盒等比缩放，量化到 `0..40` 整数 | 1 单位 ≈ 125 m（5 km 路线） |
| 5 | `render_nav.c` | `draw_overview`：`px = ax + 8 + x` → 1 单位 = 1 px | **只用 40×40，画布 60%×50% 空白** |

**等间隔采样**的直接后果：5 km 路线 16 点 → 点距约 **333 m**，于是
① 任何 <333 m 尺度的转向形状被"抄近道"成斜线；
② 拐弯恰好落在两个采样点之间时，**整个路口从行程图上消失**。

## 2. 为什么"抽拐点"也不行（用户提出的关键反例）

用户提出：一条**很长、弧度均匀**的弧线路段（大半径弯道/匝道/沿河路），两端点方位角变化很大，
但中间**没有任何明显拐点** —— 用"转角阈值抽拐点"一个点都抽不出来，弧线会被两端点连成的
直线抹平。**该反例成立，"抽拐点"方案不可用。**

## 3. 调研结论（同行做法）

| 算法 | 度量 | 强项 | 弱项 | 采用者 |
| :--- | :--- | :--- | :--- | :--- |
| Douglas-Peucker (1973) | 点到弦的**垂直距离** | 误差有硬界（≤ε）；保留直角/凸包/极值 | 高压缩下出尖刺；**起点敏感**；长缓弧可能被拉直 | Esri、SuperMap、PostGIS `simplify()`、GDAL `-simplify` |
| Visvalingam-Whyatt (1993) | 三角形**有效面积** | 平滑自然；**起点无关**；**长弧优先保留**；可按点数截断 | 无简单距离界；可能剪掉窄尖角（直角路口） | Mapshaper 默认（weighted VW）；QGIS 建议弃 DP 改用它 |

**选型依据**：VW 的度量 `面积 = ½ × 弦长 × 弧高` 天然同时满足用户的三条要求 ——
短+小角度（面积小）先删、长直段（面积 0）先删、**长而缓的弧线（弦长很大 → 面积大）自动保留**。
但 VW 会误剪窄尖角，因此叠加**直角拐点锁定**补偿 —— 这正是用户提出的
"按短、小角度排序，选最重要的 N 个点"策略的严格形式。

同时保留 **DP** 以便对照（用户要求两种都实现、设置内可切换）。

## 4. 最终方案

### 4.1 算法（`data/PolylineSampler.kt`，App 端，米制坐标）

```
A. simplifyVw（默认）：Visvalingam 面积删点 + 直角锁定 + 首尾锁定
   1) locked[0] = locked[n-1] = true
   2) 直角锁定：turnDeg(i) >= 45° 且两侧线段 >= minLegM → locked[i] = true
   3) 面积打分：area[i] = ½·|cross(P[i]-P[i-1], P[i+1]-P[i])|
   4) while (count > maxN) { 删 area 最小的未锁定点；重算其邻居面积 }

B. simplifyDp（可选）：自适应 ε 的 DP
   1) 二分 ε ∈ [0, bbox 对角线]，直到 DP(ε) 的点数 <= maxN
   2) DP 用显式栈迭代实现（防大 n 爆栈）

两者共同保证：结果 <= maxN 点，且必含首尾点。
```

参数：`maxN = 16`（协议 `NAV_MAX_PTS` 上限，**协议不变**）；`LOCK_DEG = 45°`；
`minLegM` = 行程图上约 2 px 对应的米数 = `2 / (80 / OV_SPAN) × (跨度 / (OV_SPAN - 4))`，自动计算。

### 4.2 坐标系（修正东西向拉伸）

原先 `miniMapFromGeo()` 用 `maxOf(latSpan, lonSpan)` 做统一 scale，x 直接用 `Δlon`，
**未做 cos(纬度) 折算** → 北京纬度下东西向形状被拉伸约 30%，转弯角度失真。

修正：先用 `PolylineSampler.toMeters()` 把经纬度折算成**米制**（x = 东向米、y = 北向米，
含 `cos(lat)`），采样与缩放都在米制下进行，行程图形状与真实等比。

### 4.3 分辨率与画布

| 项 | 改动前 | 改动后 |
| :--- | :--- | :--- |
| `OV_SPAN`（局部坐标跨度） | 40 | **200**（拐点分辨精度 ×5） |
| 固件映射 | `px = ax + 8 + x`（硬编码 40、1 单位 = 1 px） | `ov_to_px()`：按 `OV_SPAN` 归一化，等比铺 **72×72**（100×80 画布内留 4px 边距，右上角仍留给"北"指示） |
| 协议字段类型 | `int16_t x,y` | **不变**（0..200 仍在其范围内） |

### 4.4 当前位置黄点

改动前：`overviewDot = mini[ (progress/100) × (mini.size-1) ]` —— 按**数组索引**取点。
采样后点距不再均匀（长直段只有端点），按索引取会让黄点**跳变/卡住**。

修正：黄点改用**当前定位经纬度直接投影**到行程图坐标系（`NavState.overviewDotPos`，
与路径点同一套 `OverviewProjector` 变换），位置精确且平滑。

### 4.5 「行程预览」Tab（对照与调参）

新增第三个 Tab「行程预览」：懒加载 `WebView` → `assets/sampling_compare.html`。
算路成功后 App 用 `evaluateJavascript` 注入**路径经纬度数组**（`[[lon,lat],...]`，
米制折算与 cos 在页面内完成，保证两种算法在同一坐标系下比较）。

页面内 VW / DP **并排**渲染同一路径，各自显示：输出点数、最大偏差（px 与米）、耗时；
可调 `maxN` 与直角锁定角。同一份 HTML 在电脑浏览器双击也能打开（内置演示数据 +
粘贴 JSON 入口），**只有一份代码**。

> 注：页面在 App（WebView）内会跳过内置演示数据，只等待真实路径注入。

## 5. 改动清单（实施结果）

| # | 文件 | 动作 |
| :--- | :--- | :--- |
| 1 | `data/PolylineSampler.kt` | 新建：`simplifyVw` / `simplifyDp` / `toMeters` / `toGeo` / `sampleGeo` / `minLegMeters` |
| 2 | `data/NavStateMapper.kt` | 新增 `OverviewProjector` + `overviewProjectorFor`；`miniMapFromGeo` 改米制 + cos；`OV_SPAN` 40→200；黄点改用 `s.overviewDotPos` |
| 3 | `data/NavState.kt` | 新增字段 `overviewDotPos` |
| 4 | `data/AmapNavSource.kt` | 构造新增 `samplerMode`；`pathCoords` 改用 `sampleGeo()`；`refreshPathProjection` 算 `overviewDotPos` |
| 5 | `data/AppPrefs.kt` | 新增 `samplerMode`（默认 `vw`） |
| 6 | `res/layout/dialog_settings.xml` | 新增「行程图」分组单选 |
| 7 | `res/values/strings.xml` | 新增 `set_group_overview` / `set_overview_vw` / `set_overview_dp` / `tab_compare` |
| 8 | `res/layout/activity_main.xml` | 新增 `pageCompare` 容器 |
| 9 | `MainActivity.kt` | 第三个 Tab「行程预览」+ `applyPage(index)` + WebView 懒加载/释放/注入 + 设置页读写 + 返回键处理 |
| 10 | `app/src/main/assets/sampling_compare.html` | 新建：对比页（唯一源，电脑/手机共用） |
| 11 | `Esp32S3/main/render/render_nav.c` | 新增 `OV_SPAN`/`ov_to_px`，`draw_overview` 三处坐标改用它 |

**不动**：协议（`nav_proto`/`nav_frame.h`）、状态机、罗盘、主视图投影逻辑（见 §7）、颜色层。

> ⚠️ **跨端约定**：`OV_SPAN` 同时存在于 App（`NavStateMapper.OV_SPAN`）与固件（`render_nav.c`），
> **两者必须同步更新**。只更新一侧会导致行程图溢出或缩成一点。

## 6. 验证结果

| 项 | 命令 | 结果 |
| :--- | :--- | :--- |
| 固件静态预检 | `python -X utf8 tools/precheck.py main` | 0 问题（21 .c / 19 .h） |
| App 静态预检 | `python -X utf8 tools/kotlin_precheck.py .` | 0 问题（11 .kt / 8 .xml） |
| 对比页 JS 语法 | 提取 `<script>` 后 `node --check` | OK |
| 算法级测试 | 合成「长直 500m + 直角 + 半径 300m 长弧 + 末端直路」共 201 点 | **ALL CHECKS PASSED**：VW 输出 16 点 / DP 15 点；首尾保留；直角拐点保留；长弧未被拉直；最大偏差 0.11 / 0.15 逻辑像素 |
| APK 构建 | `./gradlew --no-daemon :app:assembleDebug` | BUILD SUCCESSFUL (43s) |
| 真机 | — | **未验证（F13 教训：交付后需用户真机启动一次）** |

## 7. 遗留（不在本次授权范围，需用户另行决定）

| # | 项 | 说明 |
| :--- | :--- | :--- |
| ① | 预览「开始导航」按钮灰色 | `showRoutePreview()` 末尾缺一行 `updatePickState()`（用户已报告，一行修复） |
| ② | 预览路线无起终点 marker | 地址输入模式下必然没有（地图选点才有），需用 `coords.first()/last()` 自动补 |
| ③ | 导航中地图无「导航画面」 | 无相机跟随 / 路线分色 / 车头箭头 / 行程图卡片 / 结束导航按钮 |
| ④ | **主视图（路面绿线）折线** | `refreshPathProjection()` 用**同一份 16 点** `pathCoords` 过滤"前方 400 m"，5 km 路线下 400 m 内只剩 1~2 点 → 屏幕绿线粗糙。修法：主视图改用 `pathAllCoords`（全量密集点）过滤 400 m，再按协议上限降采样到 16 |
| ⑤ | 绘制来源颜色分层排查 | 用户建议：把各来源线段染成不同颜色以定位漂移源头（8 处共用同一个绿色 `0x07E0`） |
| ⑥ | `past_center` 仍被 `#if 0` 屏蔽 | `render_nav.c`，等 App 数据修好后再恢复绘制 |
| ⑦ | `road_path()` 越界判断用未投影坐标 | 上轮引入的瑕疵：判 `pts[i]` 却画 `pr[i]`，且是"整段跳过"而非裁剪 |
