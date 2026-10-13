# 线稿地图样式（必需文件）

「地图样式（截图用）＝ 线稿地图」依赖高德**自定义地图样式**。请把从高德自定义地图平台导出的两个文件
放到**本目录**（`mobileApp/android/app/src/main/assets/`）：

| 文件名 | 说明 |
| :--- | :--- |
| `style.data` | **必需**。样式主体（二进制）|
| `style_extra.data` | 可选。扩展配置（背景色等）|
| `textures.zip` | 仅当样式用到纹理时需要（本项目不用）|

**缺 `style.data` 时程序不会崩**：会打印明确提示并回退到"默认"样式（设置项照常可切）。

---

## 怎么生成（一次性工作）

1. 打开 **https://geohub.amap.com/mapstyle/index**（高德自定义地图平台，需登录）
2. **新建样式 → 选择「标准模板」**（标准模板免费；基础/高级模板收费）
3. 按下面的清单改图层 → **保存并发布**
4. 发布后选 **「使用方法」→ Android → 下载离线文件**，解压得到 `style.data` / `style_extra.data`
5. 把这两个文件复制到本目录（`assets/`）

> 好消息：本项目**没有使用 `AMapNaviView`**，所以自定义样式不会被"导航模式"重置 —— 直接
> `AMap.setCustomMapStyle()` 就生效。

---

## 配置清单（做出"线稿地图 + 正常文字"）

**核心技巧**：**道路只用「描边 stroke」画线，把「填充 fill」设成白色/透明** —— 于是只剩线条，就是线稿。

| 目标 | featureType | elementType | 设为 |
| :--- | :--- | :--- | :--- |
| **背景** | `background` | `geometry` | **白 `#FFFFFF`**（或极浅灰 `#FAFAFA`）|
| **主干道** | `road.highway` | **`geometry.fill`** | **白/透明（不要色块）** |
| **主干道线条** | `road.highway` | **`geometry.stroke`** | **★ 深灰 `#444444`（这是"线"）** |
| **次干道** | `road.arterial` | `geometry.fill` | 白/透明 |
| **次干道线条** | `road.arterial` | `geometry.stroke` | 深灰 `#666666` |
| **小路** | `road.local` | `geometry.stroke` | 浅灰 `#AAAAAA`（嫌乱可整体隐藏）|
| **建筑物** | `building` | `geometry.fill` | **隐藏（visibility off）** |
| **绿地/公园** | `green` / `land` | `geometry` | 隐藏或极浅（`#F5F9F5`）|
| **水域** | `water` | `geometry.fill` | 隐藏或极浅（`#F0F4F8`）|
| **POI（店铺等）** | `poi` | `geometry` + `labels.icon` | **隐藏**（想留就保留）|
| **★ 地图文字** | **`labels.text.fill`** | — | **★ 保持深色高对比（`#222`）—— 不要动它** |
| **★ 文字描边** | `labels.text.stroke` | — | **白描边 `#FFFFFF`**（浅底上也清晰）|
| 行政区名 | `district` | `labels.text` | 保留或隐藏，看你喜好 |

**关键三条**：
1. **道路：`fill` 关掉、只用 `stroke`** → 线稿感的来源；
2. **建筑/绿地/水域：隐藏或淡化** → 去掉大面积色块（大面积色块是分光镜遮挡的主因）；
3. **★ `labels.text.*` 不要动** → **地图文字保持正常**，这样截图里的文字不会被任何轮廓处理破坏
   （这正是"地图轮廓化 + 文字保留"的实现方式）。

---

## 验证

改完设置后（APK 设置页 → **APK** Tab → **App 地图** → **地图样式（截图用）→ 线稿地图**）：

- 手机导航页面的地图应变成线稿风、**文字仍正常**；
- 用 `docs/develp/hud_lab_a2a3.html` 传这张截图，看**发光占比 / 平均发光量**是否显著下降。
