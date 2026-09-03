> - 项目：ESP32-S3 蓝牙 BLE 导航屏通信协议
> - 用途：手机 / Web ↔ ESP32-S3 BLE，传输导航帧、配置指令、事件上报；面向 TFT-LCD 320×240 导航模拟器。
> - 传输层：BLE GATT，特征值 **WRITE 用于下发指令 / 帧数据**，**NOTIFY 用于设备上报事件**。
> - 编码：UTF-8，JSON 文本，**每条 MTU 分片不超过 240 字节**；完整 JSON 帧用 `\n` 作为帧结束分隔符。
> - 版本：V1.6

## 1. GATT 服务与特征值定义

| 角色        | UUID                                   | 说明                                     | 属性                   |
| ----------- | -------------------------------------- | ---------------------------------------- | ---------------------- |
| Service     | `0000ffb0-0000-1000-8000-00805f9b34fb` | 导航 BLE 服务                            | Primary                |
| Char Write  | `0000ffb1-0000-1000-8000-00805f9b34fb` | 主机→设备：下发 JSON 指令 / 导航帧       | Write Without Response |
| Char Notify | `0000ffb2-0000-1000-8000-00805f9b34fb` | 设备→主机：状态与事件上报（DEV_STATUS / PONG） | Notify                 |

> 说明：
>
> 1. 使用 `Write Without Response`，提升吞吐；上层必须做帧边界 `\n` 分割。
> 2. BLE MTU 协商到 247；Write Without Response 单包有效载荷上限为 MTU-3=244，保守按 ≤240 字节切分；大于 240 字节的 JSON 自动分包发送，接收端按换行符 `\n` 重组完整 JSON 对象。
> 3. 所有数据包为**单行 JSON + `\n`**，禁止 JSON 内部带换行。

## 2. 通用帧格式

所有双向报文均为 JSON 对象，必须包含 `msg_type` 字段区分消息类型。

```
{
  "msg_type": "NAV_FRAME",
  "payload": {}
}
```

### msg_type 枚举

| msg_type       | 方向        | 说明                                       |
| -------------- | ----------- | ------------------------------------------ |
| `NAV_FRAME`    | 主机 → 设备 | 导航渲染帧，LCD 刷新主画面                 |
| `SET_CONFIG`   | 主机 → 设备 | 设置设备参数（动画速度、亮度、弹窗超时、显示开关） |
| `GET_CONFIG`   | 主机 → 设备 | 请求读取当前设备配置                       |
| `CLEAR_SCREEN` | 主机 → 设备 | 清屏，回到黑屏待机                         |
| `POPUP_MSG`    | 主机 → 设备 | 弹出消息弹窗                               |
| `POPUP_CLOSE`  | 主机 → 设备 | 关闭弹窗                                   |
| `DEV_STATUS`   | 设备 → 主机 | 设备状态上报（配置、版本、错误计数；err>0 时周期主动上报） |
| `PING`         | 双向        | 心跳包                                     |

> 可靠性说明：报文为 JSON 文本，帧内**不设 CRC**（依赖 BLE L2CAP 链路校验 + JSON 语法校验）；解析失败由设备计数，并通过 DEV_STATUS 的 `err` 字段上报（见 4.1）。

## 3. 主机下发报文定义

### 3.1 NAV_FRAME 导航渲染帧（核心）

> 用于驱动 LCD 绘制导航透视道路画面，对应前端模拟器 `navFrame` 结构。

```
{
  "msg_type":"NAV_FRAME",
  "payload":{
    "heading":0,
    "turn_dist":500,
    "hint":"前方500米直行",
    "total_dist":8200,
    "progress_pct":34,
    "elapsed_min":28,
    "eta_time":"14:27",
    "centerLine":[[160,130],[160,110],[160,80],[160,50],[160,20]],
    "pastCenter":[[160,130],[160,110]],
    "routeCenter":[[160,110],[160,80],[160,50],[160,20]],
    "pos":[160,110],
    "overview":[[8,6],[22,19],[46,14],[62,26]],
    "overview_dot":[22,19]
  }
}
```

字段说明：

- `heading`：罗盘航向角，0~359 度，0=北；主视图已由手机按"车头朝上"完成旋转，`heading` 仅用于罗盘指示
- `turn_dist`：下一个转向距离，单位米
- `hint`：导航提示文本
- `total_dist`：总路程，米
- `progress_pct`：进度百分比 0-100
- `elapsed_min`：已行驶分钟
- `eta_time`：预计到达时间字符串 `HH:mm`（手机端计算后下发，设备无时钟）
- `centerLine`：道路中心线屏幕坐标点数组 `[[x,y],...]`，近处点在前，远处在后
- `pastCenter`：已经驶过的路径线段（屏幕坐标）
- `routeCenter`：未来将要行驶的路径线段（屏幕坐标）
- `pos`：车辆本机坐标 `[x,y]`
- `overview`：右下角小地图路径点
- `overview_dot`：小地图当前位置点

> 坐标与上限约定（发送端必须遵守）：
>
> 1. 所有坐标均为 **LCD 屏幕像素整数坐标**（x: 0~319，y: 0~239），已由手机完成"车头朝上"旋转与平移换算；
> 2. 坐标数组点数上限：`centerLine` / `pastCenter` / `routeCenter` ≤ 16，`overview` ≤ 16；
> 3. 单条完整 JSON 报文 ≤ 2048 字节；
> 4. 文本长度上限（按字符数）：`hint` ≤ 48；`title` ≤ 32（标题区显示 1 行，超出由发送端截断）；`content` ≤ 128（弹窗内容区按 16px 点阵仅显示 **3 行 × 17 全角**，发送端预截断到 ≤48 全角并追加省略号，见 §3.5）；
> 5. `overview` / `overview_dot` 坐标为相对右下角小地图区域的像素坐标（区域左上角为原点，y 向下）；内边距与 y 翻转属渲染层，HTML 模拟器与 ESP32 保持一致；
> 6. 透视梯形路面、流动边界虚线、车道中线虚线等视觉效果由渲染端基于 `centerLine` 本地生成（HTML 与 ESP32 使用同一算法），不在帧中传输多边形。

### 3.1.1 路况模板（可选字段 maneuver / road）

> 进入路口 / 环岛 / 匝道等复杂路况前，主机可在 NAV_FRAME 附加以下**可选**字段；两者均缺省时设备按"行驶透视条带"渲染，**向后兼容**。

payload 附加字段：

- `maneuver`（字符串，可选）：当前机动类型，取值 `straight | turn_left | turn_right | roundabout_N | fork_left | fork_right`（N=环岛出口序数）。用于顶部提示与基础符号。
- `road`（对象，可选）：路况模板参数，坐标一律为**车头朝上平面像素坐标**（x: 0~319，y: 0~239），渲染端按主视图"近大远小"透视投影绘制（投影常量两端一致，见技术实现路径 1.5）：
  - `type`：`straight | curve | tjunc | cross | multi | roundabout | fork`
  - `dir`：方向参数（弯向 / 驶出出口 / 支路方向），如 `L | R | E | N | W | NE`
  - `pts`：平面中心线/支路关键点 `[[x,y],…]`（上限 ≤16，与 centerLine 一致）
  - `cx` / `cy` / `r`：`roundabout` 圆心（平面）与半径（像素）
  - `half`：路面半宽（平面像素）

> 触发（主机侧）：`turn_dist` 小于阈值（建议 200m）且机动非直行时附加 `road`；驶离路口后恢复不带 `road` 的帧。
> 叠加规则：模板 `type/dir/pts` 由手机按导航路线在该段内的**真实走向**生成；渲染顺序固定为 灰路网 → 亮绿路径（真实走向，画在路网之上）→ 车辆光标，路径以帧内路线数据为准、模板路网仅作背景示意。

### 3.2 SET_CONFIG 设置参数

```
{
  "msg_type":"SET_CONFIG",
  "payload":{
    "lcd_brightness":80,
    "dash_speed":60,
    "anim_enable":true,
    "popup_timeout":5
  }
}
```

- `lcd_brightness`：LCD 背光亮度 0-100
- `dash_speed`：道路边界虚线流动速度，像素 / 秒
- `anim_enable`：true 开启道路流动动画；false 关闭静态虚线
- `popup_timeout`：消息弹窗自动关闭超时（秒）；0=不自动关闭；默认 5

### 3.3 GET_CONFIG 获取配置

```
{"msg_type":"GET_CONFIG","payload":{}}
```

设备收到后，通过 Notify 返回 `DEV_STATUS` 携带当前配置参数。

### 3.4 CLEAR_SCREEN 清屏

```
{"msg_type":"CLEAR_SCREEN","payload":{}}
```

LCD 全部填充黑色，停止导航动画。

### 3.5 POPUP_MSG 弹窗消息

```
{
  "msg_type":"POPUP_MSG",
  "payload":{
    "title":"微信｜张三",
    "content":"今晚7点聚餐，地点老地方，记得准时过来。"
  }
}
```

> 设备端行为：收到 POPUP_MSG 立即显示弹窗并启动超时（默认 5s，可用 SET_CONFIG 的 `popup_timeout` 调整；0 表示不自动关闭）；超时后自动关闭弹窗，**不上报**；NAV_FRAME 到达**不**关闭弹窗（弹窗层独立于导航层渲染）。
>
> 显示与滚动规则：内容区可视 3 行 × 17 全角（16px 点阵）；≤3 行时静态显示（默认 5s）后自动关闭；**超长内容**：首屏停留约 4.4s（多 3s）后每 1.4s 逐行上滚，末屏停留 2.4s 自动关闭（`popup_timeout`=0 时不自动关闭、不滚动）。弹窗期间：非弹窗区域整体**置灰**、弹窗标题与消息以**绿色**渲染，关闭弹窗后恢复正常显示。
>
> 投递语义：POPUP_MSG 为**尽力投递**（Write Without Response 无应答、丢失不重试，通知事件不可重放）；链路活性由 PING/PONG 心跳保证。

### 3.6 POPUP_CLOSE 关闭弹窗

```
{"msg_type":"POPUP_CLOSE","payload":{}}
```

主机主动关闭弹窗（设备停止超时计时）。

### 3.7 PING 心跳（主机→设备）

```
{"msg_type":"PING","payload":{"ts":1786123456}}
```

> 心跳约定：主机建议每 2s 发送一次 PING，设备应答 PONG；设备连续 5s 未收到任何主机报文，或 BLE 连接断开，判定链路异常（断开时自动清屏，见 §6）。

## 4. 设备上报 Notify 报文

### 4.1 DEV_STATUS 设备状态上报

```
{
  "msg_type":"DEV_STATUS",
  "payload":{
    "lcd_brightness":80,
    "dash_speed":60,
    "anim_enable":true,
    "popup_timeout":5,
    "firmware_ver":"V1.0",
    "err":0
  }
}
```

- `lcd_brightness`：LCD 背光亮度 0-100
- `dash_speed`：道路边界虚线流动速度，像素 / 秒
- `anim_enable`：true 道路流动动画开启；false 关闭
- `popup_timeout`：当前弹窗超时配置（秒）
- `firmware_ver`：固件版本号
- `err`：错误计数（JSON 解析失败 / 非法帧累计）；err>0 时设备每 30s 主动上报一次 DEV_STATUS（Notify），直到 err 归零或收到 GET_CONFIG 应答后清零

### 4.2 PONG 心跳应答（设备→主机）

```
{"msg_type":"PONG","payload":{"ts":1786123456}}
```

## 5. 分包与帧重组规则（ESP32 接收逻辑）

1. BLE 写特征接收字节流，写入环形缓冲区。
2. 扫描缓冲区查找 `\n` 换行符；
3. 遇到 `\n`，截取从上次结束位置到换行，作为一条完整 JSON 报文；
4. JSON 字符串做 `JSON.parse()`；解析失败直接丢弃该帧，并累加 `err` 计数；
5. 缓冲区超过最大长度（4096 字节）直接清空，防止脏数据堆积（单帧 JSON 上限 2048 字节，见 §3.1）。

> 主机侧（Web 模拟器）发送逻辑：
>
> - 将 JSON 压缩为单行，末尾追加 `\n`；
> - 如果字符串长度 > 240 字节，自动分片多次 Write（Write Without Response 在 MTU 247 下有效载荷上限为 244 字节，保守按 240 切分）；接收端依靠换行重组。

## 6. 错误处理约定

1. 非法 JSON：设备丢弃，累加 `err` 计数，不回复；
2. 未知 `msg_type`：直接忽略；
3. 字段缺失：使用内置默认值渲染，不崩溃；
4. BLE 断开：设备自动清屏，回到待机状态；
5. 弹窗超时自动关闭：设备按 `popup_timeout`（默认 5s）自动关闭弹窗，不依赖主机 POPUP_CLOSE（见 §3.5）。
6. 导航帧超时：连续 3s 未收到 NAV_FRAME（BLE 连接与心跳正常）→ 屏幕顶部显示"信号中断"并停止流动虚线动画；收到新 NAV_FRAME 后自动恢复刷新。

## 7. Web 模拟器与 ESP32 交互时序示例

```
Web(主机)                          ESP32-S3(设备)
    |  connect BLE                  |
    |  subscribe FF B2 Notify       |
    |  GET_CONFIG ----------------->|
    |                               |----> DEV_STATUS (Notify)
    |  NAV_FRAME ------------------>| 渲染导航画面，驱动流动虚线动画
    |  POPUP_MSG ------------------>| 弹出消息窗口
    |  POPUP_CLOSE ---------------->| 关闭弹窗
    |  SET_CONFIG(dash_speed=40)--->| 修改虚线流动速度
    |  PING(ts=xxx) --------------->|
    |                               |----> PONG(ts=xxx)
    |  CLEAR_SCREEN --------------->| 黑屏待机
```

## 8. ESP32 固件数据结构映射提示

C 伪代码示例，用于解析 NAV_FRAME payload：

```
typedef struct {
  int16_t heading;
  int16_t turn_dist;
  char hint[48];
  int32_t total_dist;
  uint8_t progress_pct;
  uint16_t elapsed_min;
  char eta_time[16];
} nav_frame_t;
```

> 注意：`centerLine`、`pastCenter`、`routeCenter`、`overview` 为变长坐标数组；协议约束最大点数量 ≤16 个点（见 §3.1），ESP32 按 16 点上限分配缓冲区。

## 9. 修订记录

| 版本 | 日期       | 变更内容                                                     |
| ---- | ---------- | ------------------------------------------------------------ |
| V1.0 | 2026-09-02 | 初始完整版本；定义全部消息、GATT UUID、分包规则；对齐 Web 模拟器字段 |
| V1.1 | 2026-09-02 | 确认 JSON 为协议基准（原二进制帧方案废弃）：移除无硬件来源字段（key_event/vbat/percent）；DEV_STATUS 增加 `err` 错误计数并明确帧内不设 CRC；补充设备端行为约定（弹窗超时自动关闭、NAV_FRAME 不干预弹窗、PING 2s/5s 心跳）；新增坐标与上限约定（点数 ≤16、单帧 ≤2048B、文本上限、overview 坐标基准）；修正 MTU 分包说明（MTU 247 下有效载荷上限 244，按 240 切分） |
| V1.2 | 2026-09-02 | 补充数据流歧义裁决：导航帧超时兜底（3s 无 NAV_FRAME → 顶部"信号中断"提示并停动画，恢复后自动刷新）；POPUP_MSG 明示尽力投递、丢失不重试；DEV_STATUS `err` 上报触发（err>0 每 30s 主动 Notify，直至清零） |
| V1.3 | 2026-09-02 | 对齐弹窗可视容量（16px 点阵）：title 标题区 1 行；content 内容区 3 行 × 17 全角、发送端预截断到 ≤48 全角并追加省略号；明确 hint/title/content 截断规则（见 §3.1 第 4 条） |
| V1.5 | 2026-09-02 | 新增复杂路况模板（B+D 方案）：NAV_FRAME 可选 `maneuver` 与 `road` 字段（7 类模板、平面像素坐标、向后兼容，见 §3.1.1）；模板触发/叠加规则；视觉原型见 nav_sim_v2.html（主视图集成、近大远小投影）与 nav_sim_v3.html（整屏样例） |
| V1.6 | 2026-09-02 | 行为澄清（HTML V2 视觉验收冻结）：弹窗显示与滚动规则（3 行×17 全角；超长首屏多停 3s 后每 1.4s 逐行上滚、末屏 2.4s 关）；弹窗期间非弹窗区域置灰、弹窗文字绿色；模板近端路面宽度基准较初版 +1/3，流动虚线仅绘制在导航路径所在道路（见技术实现路径 1.5/2.1） |

------
