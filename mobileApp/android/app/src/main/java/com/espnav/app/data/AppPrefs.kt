package com.espnav.app.data

import android.content.Context

/**
 * 习惯性配置的持久化（SharedPreferences）。
 *
 * 由「连接」页的「⚙ 设置」对话框读写；各处取用默认值时统一走这里，
 * 避免把地址/城市/缩放等写死在代码里（此前 geoCity 写死"北京"，用户不在北京时算路会失败）。
 */
class AppPrefs(ctx: Context) {

    private val sp = ctx.getSharedPreferences(NAME, Context.MODE_PRIVATE)

    /** 设备地址（连接候选首选项） */
    var host: String
        get() = sp.getString(K_HOST, DEF_HOST) ?: DEF_HOST
        set(v) = sp.edit().putString(K_HOST, v).apply()

    /** 设备端口 */
    var port: Int
        get() = sp.getInt(K_PORT, DEF_PORT)
        set(v) = sp.edit().putInt(K_PORT, v).apply()

    /** App 冷启动后是否自动发起一次连接（默认关：避免用户手动断开后又被连上） */
    var autoConnectOnStart: Boolean
        get() = sp.getBoolean(K_AUTO_CONN, false)
        set(v) = sp.edit().putBoolean(K_AUTO_CONN, v).apply()

    /** 默认起点地址 */
    var fromAddress: String
        get() = sp.getString(K_FROM, DEF_FROM) ?: DEF_FROM
        set(v) = sp.edit().putString(K_FROM, v).apply()

    /** 默认终点地址 */
    var toAddress: String
        get() = sp.getString(K_TO, DEF_TO) ?: DEF_TO
        set(v) = sp.edit().putString(K_TO, v).apply()

    /** 是否自动用定位取"当前城市"（用于地址解析/算路）；关掉后用 geoCity 的值 */
    var autoCity: Boolean
        get() = sp.getBoolean(K_AUTO_CITY, true)
        set(v) = sp.edit().putBoolean(K_AUTO_CITY, v).apply()

    /** 手动指定的城市（autoCity=false 时使用） */
    var geoCity: String
        get() = sp.getString(K_CITY, "北京") ?: "北京"
        set(v) = sp.edit().putString(K_CITY, v).apply()

    /** 地图默认缩放级别（进入导航页的视野） */
    var defaultZoom: Float
        get() = sp.getFloat(K_ZOOM, 16f)
        set(v) = sp.edit().putFloat(K_ZOOM, v).apply()

    /** 日志区最多保留行数 */
    var maxLogLines: Int
        get() = sp.getInt(K_LOG, 1000)
        set(v) = sp.edit().putInt(K_LOG, v).apply()

    /* ---- 调试：ESP 屏显示哪些元素（用于逐个模块定位绘制问题；默认全开）---- */

    /** 一键全关：只留文字与罗盘。用来一步判断"折线漂移"是否来自这些可控图元 ——
     *  若全关后漂移线仍在，说明它不属于道路图案/路径线/行程图/车头这几类。 */
    var dbgAllOff: Boolean
        get() = sp.getBoolean(K_DBG_ALLOFF, false)
        set(v) = sp.edit().putBoolean(K_DBG_ALLOFF, v).apply()

    /** 是否绘制「道路图案」（road 模板）。关掉后固件改用真实路径画路面，可对比两种路形。 */
    var dbgShowRoad: Boolean
        get() = sp.getBoolean(K_DBG_ROAD, true)
        set(v) = sp.edit().putBoolean(K_DBG_ROAD, v).apply()

    /** 是否绘制「车道中线虚线 + fallback 真实路形路面」—— ESP 主视图上为**青色**（center_line） */
    var dbgShowCenterLn: Boolean
        get() = sp.getBoolean(K_DBG_CLN, true)
        set(v) = sp.edit().putBoolean(K_DBG_CLN, v).apply()

    /** 是否绘制「行程图」 */
    var dbgShowOverview: Boolean
        get() = sp.getBoolean(K_DBG_OV, true)
        set(v) = sp.edit().putBoolean(K_DBG_OV, v).apply()

    /** 是否绘制「车头三角」 */
    var dbgShowCar: Boolean
        get() = sp.getBoolean(K_DBG_CAR, true)
        set(v) = sp.edit().putBoolean(K_DBG_CAR, v).apply()

    /** 导航仿真（模拟行进）：开启时由高德 SDK 按路线模拟推进 —— 室内/没上车也能看效果；
     *  道路实测必须【关闭】，否则导航不跟你的真实位置（用户要求做成可切换）。 */
    /** 【M2.4】是否显示 App 导航页右下角的「行程图卡片」（App 侧）。
     *  与 ESP 端行程图小地图对应；关掉可对比观察。 */
    var dbgTripCard: Boolean
        get() = sp.getBoolean(K_DBG_TRIP, true)
        set(v) = sp.edit().putBoolean(K_DBG_TRIP, v).apply()

    var emulate: Boolean
        get() = sp.getBoolean(K_EMULATE, true)
        set(v) = sp.edit().putBoolean(K_EMULATE, v).apply()

    /** 行程图采样方式：PolylineSampler.MODE_VW（Visvalingam，默认）/ MODE_DP（道格拉斯-普克） */
    var samplerMode: String
        get() = sp.getString(K_SAMPLER, PolylineSampler.MODE_VW) ?: PolylineSampler.MODE_VW
        set(v) = sp.edit().putString(K_SAMPLER, v).apply()

    /* ================= 【M1 静态图导航】ESP 屏叠加层可读性 =================
     * 这些参数作用在“地图底图之上”的罗盘 / 文字 / 行程图 / 时间四类半透明暗底衬，
     * 以及行程图网格亮度。**语义**：透明度 0 = 全黑底衬，100 = 不铺底衬
     * （值越大越透、背景越明显）。
     * 下发时机：① 连接成功后自动下发一次 ② 在设置里拖动时实时下发（立即生效）。
     * 固件侧不落 NVS，持久化就在这里（并可随配置文件导出/导入）。 */

    /** 半透明暗底衬总开关 */
    var espScrimOn: Boolean
        get() = sp.getBoolean(K_ESCRIM_ON, true)
        set(v) = sp.edit().putBoolean(K_ESCRIM_ON, v).apply()

    /** 罗盘底衬透明度 0-100 */
    var espScrimCompass: Int
        get() = sp.getInt(K_ESCRIM_COMPASS, 65)
        set(v) = sp.edit().putInt(K_ESCRIM_COMPASS, v.coerceIn(0, 100)).apply()

    /** 文字底衬透明度（路名 / 导航提示 / 距离 / 左下统计） */
    var espScrimText: Int
        get() = sp.getInt(K_ESCRIM_TEXT, 65)
        set(v) = sp.edit().putInt(K_ESCRIM_TEXT, v.coerceIn(0, 100)).apply()

    /** 行程图底衬透明度（比其它更实一点：网格线本身偏暗） */
    var espScrimRoute: Int
        get() = sp.getInt(K_ESCRIM_ROUTE, 40)
        set(v) = sp.edit().putInt(K_ESCRIM_ROUTE, v.coerceIn(0, 100)).apply()

    /** 时间底衬透明度 */
    var espScrimClock: Int
        get() = sp.getInt(K_ESCRIM_CLOCK, 65)
        set(v) = sp.edit().putInt(K_ESCRIM_CLOCK, v.coerceIn(0, 100)).apply()

    /* ---- 【M6】底图地图样式（截图观感的真正决定项）----
     * 0 = 普通地图（黄白全彩，默认）；1 = 导航地图（“全蓝”，突出路网/弱化无关信息）；
     * 2 = 夜景（暗色）。高德地图 SDK 的 setMapType，不额外计费。 */
    var mapStyle: Int
        get() = sp.getInt(K_MAPSTYLE, 0)
        set(v) = sp.edit().putInt(K_MAPSTYLE, v.coerceIn(0, 3)).apply()

    /* ---- 【M3.2】地图底图：导航中按间隔推送截图（App 侧定时） ---- */

    /** 是否推送地图底图（关掉后不再发 IMG_*，ESP 保持当前画面） */
    var espMapShotOn: Boolean
        get() = sp.getBoolean(K_EMAPSHOT, true)
        set(v) = sp.edit().putBoolean(K_EMAPSHOT, v).apply()

    /** 底图刷新间隔（毫秒），限定 500–5000。
     *  下限 500ms：ESP 解码一张 320×240 JPEG 约 100–200ms，再密会在设备侧堆积；
     *  上限 5000ms：省流量/省电。默认 3000ms（比 M1 的 1s 更保守，先验证稳定性）。 */
    var espMapShotIntervalMs: Int
        get() = sp.getInt(K_EMAPSHOT_INT, 3000)
        set(v) = sp.edit().putInt(K_EMAPSHOT_INT, v.coerceIn(MAP_SHOT_INT_MIN, MAP_SHOT_INT_MAX)).apply()

    /** 【M2.5】屏幕内容水平翻转（分光镜 HUD）：默认开。
     *  分光镜（半透半反镜）让人眼看到的是左右镜像画面，所以 ESP 端送屏前要预先水平镜像。
     *  用字面 key（暂不进配置文件导出项），后续若要纳入配置再补常量与导出。 */
    var screenFlip: Boolean
        get() = sp.getBoolean("screen_flip", true)
        set(v) = sp.edit().putBoolean("screen_flip", v).apply()

    /** 行程图网格亮度 0-100（主格线每 50px 再亮一档） */
    /* ================= 【M2.4】ESP 屏颜色（全部可在设置里改）=================
     * 值 = RGB565（十进制）；0 表示"用固件默认色"。
     * 设置界面用预设色下拉选择（色板表在 AppPrefs.ESP_COLORS）。 */

    /** 主色：文字 / 罗盘 / 路名 / 统计 / 时间 / fallback 中心线（默认绿 0x07E0 = 2016） */
    var espColMain: Int
        get() = sp.getInt(K_ECOL_MAIN, 2016)
        set(v) = sp.edit().putInt(K_ECOL_MAIN, v).apply()

    /** 行程图轨迹线（默认亮蓝 0x5D9F = 23967） */
    var espColTrack: Int
        get() = sp.getInt(K_ECOL_TRACK, 23967)
        set(v) = sp.edit().putInt(K_ECOL_TRACK, v).apply()

    /** 行程图网格基准色（默认灰绿 0x8450 = 33872） */
    var espColGrid: Int
        get() = sp.getInt(K_ECOL_GRID, 33872)
        set(v) = sp.edit().putInt(K_ECOL_GRID, v).apply()

    /** 路面灰（默认 0x73AE = 29614） */
    var espColRoad: Int
        get() = sp.getInt(K_ECOL_ROAD, 29614)
        set(v) = sp.edit().putInt(K_ECOL_ROAD, v).apply()

    /** 车头三角（默认黄 0xFFE0 = 65504） */
    var espColCar: Int
        get() = sp.getInt(K_ECOL_CAR, 65504)
        set(v) = sp.edit().putInt(K_ECOL_CAR, v).apply()

    /** 提示 / 警示行文字（默认黄 0xFFE0 = 65504） */
    var espColHint: Int
        get() = sp.getInt(K_ECOL_HINT, 65504)
        set(v) = sp.edit().putInt(K_ECOL_HINT, v).apply()

    var espGridBright: Int
        get() = sp.getInt(K_EGRID, 55)
        set(v) = sp.edit().putInt(K_EGRID, v.coerceIn(0, 100)).apply()

    // ---------------- 独立配置文件（导出/导入） ----------------

    /** 把当前全部配置导出成 JSON（供写入外部配置文件） */
    fun exportToJson(): String = org.json.JSONObject().apply {
        put("host", host)
        put("port", port)
        put("autoConnectOnStart", autoConnectOnStart)
        put("fromAddress", fromAddress)
        put("toAddress", toAddress)
        put("autoCity", autoCity)
        put("geoCity", geoCity)
        put("defaultZoom", defaultZoom.toDouble())
        put("maxLogLines", maxLogLines)
        put("samplerMode", samplerMode)
        put("emulate", emulate)
        put("dbgAllOff", dbgAllOff)
        put("dbgShowRoad", dbgShowRoad)
        put("dbgShowCenterLn", dbgShowCenterLn)
        put("dbgShowOverview", dbgShowOverview)
        put("dbgShowCar", dbgShowCar)
        put("espScrimOn", espScrimOn)
        put("espScrimCompass", espScrimCompass)
        put("espScrimText", espScrimText)
        put("espScrimRoute", espScrimRoute)
        put("espScrimClock", espScrimClock)
        put("espGridBright", espGridBright)
        put("dbgTripCard", dbgTripCard)
        put("espColMain", espColMain)
        put("espColTrack", espColTrack)
        put("espColGrid", espColGrid)
        put("espColRoad", espColRoad)
        put("espColCar", espColCar)
        put("espColHint", espColHint)
        put(
            "savedAt",
            java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss", java.util.Locale.US)
                .format(java.util.Date())
        )
    }.toString(2)

    /** 用 JSON 覆盖当前配置；返回成功导入的项数（缺字段的项会保留原值） */
    fun importFromJson(json: String): Int {
        val o = runCatching { org.json.JSONObject(json) }.getOrNull() ?: return 0
        var n = 0
        if (o.has("host")) { host = o.optString("host", host); n++ }
        if (o.has("port")) { port = o.optInt("port", port); n++ }
        if (o.has("autoConnectOnStart")) { autoConnectOnStart = o.optBoolean("autoConnectOnStart", autoConnectOnStart); n++ }
        if (o.has("fromAddress")) { fromAddress = o.optString("fromAddress", fromAddress); n++ }
        if (o.has("toAddress")) { toAddress = o.optString("toAddress", toAddress); n++ }
        if (o.has("autoCity")) { autoCity = o.optBoolean("autoCity", autoCity); n++ }
        if (o.has("geoCity")) { geoCity = o.optString("geoCity", geoCity); n++ }
        if (o.has("defaultZoom")) { defaultZoom = o.optDouble("defaultZoom", defaultZoom.toDouble()).toFloat(); n++ }
        if (o.has("maxLogLines")) { maxLogLines = o.optInt("maxLogLines", maxLogLines); n++ }
        if (o.has("samplerMode")) { samplerMode = o.optString("samplerMode", samplerMode); n++ }
        if (o.has("emulate")) { emulate = o.optBoolean("emulate", emulate); n++ }
        if (o.has("dbgAllOff")) { dbgAllOff = o.optBoolean("dbgAllOff", dbgAllOff); n++ }
        if (o.has("dbgShowRoad")) { dbgShowRoad = o.optBoolean("dbgShowRoad", dbgShowRoad); n++ }
        if (o.has("dbgShowCenterLn")) { dbgShowCenterLn = o.optBoolean("dbgShowCenterLn", dbgShowCenterLn); n++ }
        if (o.has("dbgShowOverview")) { dbgShowOverview = o.optBoolean("dbgShowOverview", dbgShowOverview); n++ }
        if (o.has("dbgShowCar")) { dbgShowCar = o.optBoolean("dbgShowCar", dbgShowCar); n++ }
        if (o.has("espScrimOn")) { espScrimOn = o.optBoolean("espScrimOn", espScrimOn); n++ }
        if (o.has("espScrimCompass")) { espScrimCompass = o.optInt("espScrimCompass", espScrimCompass); n++ }
        if (o.has("espScrimText")) { espScrimText = o.optInt("espScrimText", espScrimText); n++ }
        if (o.has("espScrimRoute")) { espScrimRoute = o.optInt("espScrimRoute", espScrimRoute); n++ }
        if (o.has("espScrimClock")) { espScrimClock = o.optInt("espScrimClock", espScrimClock); n++ }
        if (o.has("espGridBright")) { espGridBright = o.optInt("espGridBright", espGridBright); n++ }
        if (o.has("dbgTripCard")) { dbgTripCard = o.optBoolean("dbgTripCard", dbgTripCard); n++ }
        if (o.has("espColMain")) { espColMain = o.optInt("espColMain", espColMain); n++ }
        if (o.has("espColTrack")) { espColTrack = o.optInt("espColTrack", espColTrack); n++ }
        if (o.has("espColGrid")) { espColGrid = o.optInt("espColGrid", espColGrid); n++ }
        if (o.has("espColRoad")) { espColRoad = o.optInt("espColRoad", espColRoad); n++ }
        if (o.has("espColCar")) { espColCar = o.optInt("espColCar", espColCar); n++ }
        if (o.has("espColHint")) { espColHint = o.optInt("espColHint", espColHint); n++ }
        return n
    }

    companion object {
        private const val NAME = "espnav_prefs"
        private const val K_HOST = "host"
        private const val K_PORT = "port"
        private const val K_AUTO_CONN = "auto_connect"
        private const val K_FROM = "from_addr"
        private const val K_TO = "to_addr"
        private const val K_AUTO_CITY = "auto_city"
        private const val K_CITY = "geo_city"
        private const val K_ZOOM = "default_zoom"
        private const val K_LOG = "max_log_lines"
        private const val K_SAMPLER = "sampler_mode"
        private const val K_EMULATE = "emulate"
        private const val K_DBG_ROAD = "dbg_road"
        private const val K_DBG_CLN = "dbg_cln"
        private const val K_DBG_ALLOFF = "dbg_all_off"
        private const val K_DBG_OV = "dbg_overview"
        private const val K_DBG_CAR = "dbg_car"
        /* M1：ESP 屏叠加层可读性 */
        private const val K_ESCRIM_ON = "esp_scrim_on"
        private const val K_ESCRIM_COMPASS = "esp_scrim_compass"
        private const val K_ESCRIM_TEXT = "esp_scrim_text"
        private const val K_ESCRIM_ROUTE = "esp_scrim_route"
        private const val K_ESCRIM_CLOCK = "esp_scrim_clock"
        private const val K_EGRID = "esp_grid_bright"
        /* M3.2：地图底图 */
        private const val K_EMAPSHOT = "esp_map_shot_on"
        /* M6：底图地图样式（0 普通 / 1 导航全蓝 / 2 夜景） */
        private const val K_MAPSTYLE = "map_style"
        private const val K_EMAPSHOT_INT = "esp_map_shot_interval_ms"
        private const val K_DBG_TRIP = "dbg_trip_card"
        /* M2.4：ESP 屏颜色（RGB565 十进制） */
        private const val K_ECOL_MAIN = "esp_col_main"
        private const val K_ECOL_TRACK = "esp_col_track"
        private const val K_ECOL_GRID = "esp_col_grid"
        private const val K_ECOL_ROAD = "esp_col_road"
        private const val K_ECOL_CAR = "esp_col_car"
        private const val K_ECOL_HINT = "esp_col_hint"

        /** 【M2.4】预设色板（RGB565 十进制）—— 与 strings.xml 的 esp_color_names 一一对应 */
        val ESP_COLORS = intArrayOf(
            2016,    // 0 绿   #00E676
            23967,   // 1 蓝   #5CB0FF
            1855,    // 2 青   #00E5FF
            65280,   // 3 黄   #FFE000
            64704,   // 4 橙   #FF9800
            64138,   // 5 红   #FF5252
            64123,   // 6 品红 #FF4FD8
            65535,   // 7 白   #FFFFFF
            48631,   // 8 浅灰 #BDBDBD
            25356    // 9 深灰 #616161
        )

        /** 【M2.7】默认设备地址 = 手机热点下 ESP 的实际地址（用户 2026-09-21 要求）。
         *  旧默认 `192.168.4.1` 是 softAP 配网网关，只在"刚进过配网页"时才对；
         *  日常使用时 ESP 连手机热点拿到 192.168.43.117 —— 默认值不一致时，每次重装
         *  都得先用配置文件覆盖才能连上。192.168.4.1 仍保留在「一键连接」候选里作兜底。 */
        /** 【M3.2】底图刷新间隔范围（ms）：与设置页滑杆一致 */
        const val MAP_SHOT_INT_MIN = 500
        const val MAP_SHOT_INT_MAX = 5000

        const val DEF_HOST = "192.168.43.117"
        const val DEF_PORT = 8899
        const val DEF_FROM = "北京亦庄泰河三街1号"
        const val DEF_TO = "北京亦庄同济南路地铁站"
    }
}
