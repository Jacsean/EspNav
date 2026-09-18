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

    /** 是否绘制「道路图案」（road 模板）。关掉后固件改用真实路径画路面，可对比两种路形。 */
    var dbgShowRoad: Boolean
        get() = sp.getBoolean(K_DBG_ROAD, true)
        set(v) = sp.edit().putBoolean(K_DBG_ROAD, v).apply()

    /** 是否绘制「未走路径」折线 —— ESP 主视图上为**红色**（route_center） */
    var dbgShowRoute: Boolean
        get() = sp.getBoolean(K_DBG_ROUTE, true)
        set(v) = sp.edit().putBoolean(K_DBG_ROUTE, v).apply()

    /** 是否绘制「已走路径」折线 —— ESP 主视图上为**蓝色**（past_center） */
    var dbgShowPast: Boolean
        get() = sp.getBoolean(K_DBG_PAST, true)
        set(v) = sp.edit().putBoolean(K_DBG_PAST, v).apply()

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

    /** 行程图采样方式：PolylineSampler.MODE_VW（Visvalingam，默认）/ MODE_DP（道格拉斯-普克） */
    var samplerMode: String
        get() = sp.getString(K_SAMPLER, PolylineSampler.MODE_VW) ?: PolylineSampler.MODE_VW
        set(v) = sp.edit().putString(K_SAMPLER, v).apply()

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
        private const val K_DBG_ROAD = "dbg_road"
        private const val K_DBG_ROUTE = "dbg_route"
        private const val K_DBG_PAST = "dbg_past"
        private const val K_DBG_CLN = "dbg_cln"
        private const val K_DBG_OV = "dbg_overview"
        private const val K_DBG_CAR = "dbg_car"

        const val DEF_HOST = "192.168.4.1"
        const val DEF_PORT = 8899
        const val DEF_FROM = "北京亦庄泰河三街1号"
        const val DEF_TO = "北京亦庄同济南路地铁站"
    }
}
