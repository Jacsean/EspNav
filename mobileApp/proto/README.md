# mobileApp/proto — 手机端「路况模板几何生成器」原型（方案 A）

纯 JS/CommonJS 逻辑原型（node 可直接运行，无需 Android 工具链），
对应实施计划 **阶段 4-1**：机动识别 → `turn_dist<200m` 附加 `road` → 平面坐标换算。
后续以 Kotlin 移植进 Android 工程（同一目录或 `mobileApp/app/src/main/java/…`）。

## 文件
- `road_gen.js`        生成器模块（road/pts/exits 输出，协议 V1.8 / 几何规格 V0.3）
- `test_road_gen.js`   结构断言（node test_road_gen.js，全绿）

## 运行
node road_gen.js          # 打印可粘贴进 nav_sim_v2.html jsonInput 的示例 NAV_FRAME
node test_road_gen.js     # 断言

## 输入模型（buildFrame nav 参数）
见 road_gen.js 头部注释。关键约定：
- 车前局部路线 `pathM` 为车辆系米制点列（x=右、y=前）；
- `maneuver` 取值 straight/turn_left/turn_right/roundabout/fork_left/fork_right/curve/uturn；
- roundabout 出口计数按 O1：`rbtSdkExit`(SDK 播报) + `rbtSdkIncludesEntry`(是否含入口) → `dir=exitN`（几何口径），
  实测高德/百度后以适配参数固化（实施计划风险 5）。
