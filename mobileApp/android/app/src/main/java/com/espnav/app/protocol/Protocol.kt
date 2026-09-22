package com.espnav.app.protocol

import org.json.JSONArray
import org.json.JSONObject

/**
 * 协议层（V1.10 / V1.14）：出站报文构建与入站报文解析。
 * 仅做 JSON 组装/提取，不涉及网络（网络见 net/EspNavClient.kt）。
 */

/** 路况模板（协议 §3.1.1；字段与固件 nav_road_t、HTML V2 readRoad 对齐） */
data class RoadSpec(
    val type: String,
    val dir: String? = null,
    val exits: List<String>? = null,
    val pts: List<Pair<Int, Int>>? = null,
    val half: Int? = null,
    val cx: Int? = null,
    val cy: Int? = null,
    val r: Int? = null
) {
    fun toJson(): JSONObject = JSONObject().apply {
        put("type", type)
        if (dir != null) put("dir", dir)
        if (exits != null) put("exits", JSONArray(exits))
        if (pts != null) put("pts", Json.pts(pts))   // 已 smart-cast，无需 !!
        if (half != null) put("half", half)
        if (cx != null) put("cx", cx)
        if (cy != null) put("cy", cy)
        if (r != null) put("r", r)
    }
}

/** 导航帧（协议 §3.1） */
data class NavFrame(
    val heading: Int = 0,
    val turnDist: Int = 0,
    val hint: String = "",
    val totalDist: Int = 0,
    val progressPct: Int = 0,
    val elapsedMin: Int = 0,
    val etaTime: String = "",
    val centerLine: List<Pair<Int, Int>> = emptyList(),
    val pastCenter: List<Pair<Int, Int>> = emptyList(),
    val routeCenter: List<Pair<Int, Int>> = emptyList(),
    val pos: Pair<Int, Int>? = null,
    val overview: List<Pair<Int, Int>> = emptyList(),
    val overviewDot: Pair<Int, Int>? = null,
    val road: RoadSpec? = null,
    /** 当前速度（km/h，供固件/串口日志显示） */
    val speedKmh: Int = 0,
    /** 当前路段名称（固件显示在屏幕第一行，超长自动滚动） */
    val roadName: String = "",
    /** 提示文本（路况/测速等；固件显示在“距离”下方的黄色行） */
    val notice: String = "",
    /** 【M1.2】时:分:秒（"HH:MM:SS"）—— ESP 无 RTC，时间必须由 App 每帧下发；
     *  固件显示在主视图右下角，空串则不显示。 */
    val clock: String = ""
) {
    fun payloadJson(): JSONObject = JSONObject().apply {
        put("heading", heading)
        put("turn_dist", turnDist)
        put("hint", hint)
        put("total_dist", totalDist)
        put("progress_pct", progressPct)
        put("elapsed_min", elapsedMin)
        put("eta_time", etaTime)
        put("centerLine", Json.pts(centerLine))
        put("pastCenter", Json.pts(pastCenter))
        put("routeCenter", Json.pts(routeCenter))
        pos?.let { put("pos", Json.pair(it)) }
        put("overview", Json.pts(overview))
        overviewDot?.let { put("overview_dot", Json.pair(it)) }
        road?.let { put("road", it.toJson()) }
        put("speed", speedKmh)
        if (roadName.isNotEmpty()) put("roadName", roadName)
        if (notice.isNotEmpty()) put("notice", notice)
        if (clock.isNotEmpty()) put("clock", clock)     /* 【M1.2】时间（主视图右下角） */
    }

    fun toJsonLine(): String =
        JSONObject().put("msg_type", "NAV_FRAME").put("payload", payloadJson()).toString()
}

/** JSON 小工具 */
object Json {
    fun pair(p: Pair<Int, Int>): JSONArray = JSONArray().put(p.first).put(p.second)

    fun pts(list: List<Pair<Int, Int>>): JSONArray =
        JSONArray().apply { list.forEach { put(pair(it)) } }

    fun obj(vararg kv: Pair<String, Any>): JSONObject =
        JSONObject().apply { kv.forEach { put(it.first, it.second) } }
}

/** 出站控制报文（协议 §3.2~§3.7） */
object OutMsg {
    fun navFrame(frame: NavFrame): String = frame.toJsonLine()

    /** 主动断开通知：ESP 收到后立即回到“等待手机App连接”（协议 V1.13） */
    fun bye(): String =
        Json.obj("msg_type" to "BYE", "payload" to JSONObject()).toString()

    /** 握手：App 上线声明（ESP 收到后回 HELLO_ACK，据此进入“已连接”阶段） */
    fun hello(): String =
        Json.obj("msg_type" to "HELLO", "payload" to JSONObject()).toString()

    fun ping(ts: Long): String =
        Json.obj("msg_type" to "PING", "payload" to Json.obj("ts" to ts)).toString()

    /** 【M2.6】时间下发：ESP 无 RTC，App 连接后每秒发一次；待机画面也据此显示时间 */
    fun clock(hhmmss: String): String =
        Json.obj("msg_type" to "CLOCK", "payload" to Json.obj("clock" to hhmmss)).toString()

    /* ---- 【M3.1】地图底图分块传输（协议 V1.14）----
     * IMG_BEGIN(seq,w,h,x,y,bytes) → N×IMG_CHUNK(seq, d=base64) → IMG_END(seq)。
     * 固件按 seq 丢旧保新；单帧 ≤2048B（每块原始 ≤1200 → base64 ≤1600 字符）。 */
    fun imgBegin(seq: Int, w: Int, h: Int, x: Int, y: Int, bytes: Int): String =
        Json.obj(
            "msg_type" to "IMG_BEGIN",
            "payload" to Json.obj(
                "seq" to seq, "w" to w, "h" to h, "x" to x, "y" to y,
                "fmt" to "jpg", "bytes" to bytes
            )
        ).toString()

    fun imgChunk(seq: Int, b64: String): String =
        Json.obj("msg_type" to "IMG_CHUNK", "payload" to Json.obj("seq" to seq, "d" to b64)).toString()

    fun imgEnd(seq: Int): String =
        Json.obj("msg_type" to "IMG_END", "payload" to Json.obj("seq" to seq)).toString()

    fun getConfig(): String =
        Json.obj("msg_type" to "GET_CONFIG", "payload" to JSONObject()).toString()

    fun clearScreen(): String =
        Json.obj("msg_type" to "CLEAR_SCREEN", "payload" to JSONObject()).toString()

    /** SET_CONFIG（协议 §3.2）：只下发非 null 的字段。
     *  【M1】新增 6 项 ESP 屏叠加层可读性参数（scrim_* / grid_bright），
     *  语义：透明度 0=全黑底衬、100=不铺底衬。 */
    fun setConfig(
        brightness: Int? = null,
        dashSpeed: Int? = null,
        animEnable: Boolean? = null,
        popupTimeout: Int? = null,
        scrimOn: Boolean? = null,
        scrimCompass: Int? = null,
        scrimText: Int? = null,
        scrimRoute: Int? = null,
        scrimClock: Int? = null,
        gridBright: Int? = null,
        screenFlip: Boolean? = null,
        /* 【M9】整屏垂直镜像（上下翻转） */
        screenFlipY: Boolean? = null,
        /* 【M2.4】ESP 屏颜色（RGB565 十进制；0 = 用固件默认色） */
        colMain: Int? = null,
        colTrack: Int? = null,
        colGrid: Int? = null,
        colRoad: Int? = null,
        colCar: Int? = null,
        colHint: Int? = null,
        /* 【M7】APK 地图底图总开关：false = 固件丢弃底图、回到原始导航模式 */
        imgOn: Boolean? = null
    ): String {
        val p = JSONObject()
        brightness?.let { p.put("lcd_brightness", it) }
        dashSpeed?.let { p.put("dash_speed", it) }
        animEnable?.let { p.put("anim_enable", it) }
        popupTimeout?.let { p.put("popup_timeout", it) }
        scrimOn?.let { p.put("scrim_on", it) }
        scrimCompass?.let { p.put("scrim_compass", it) }
        scrimText?.let { p.put("scrim_text", it) }
        scrimRoute?.let { p.put("scrim_route", it) }
        scrimClock?.let { p.put("scrim_clock", it) }
        gridBright?.let { p.put("grid_bright", it) }
        /* 【M2.5】分光镜 HUD：整屏水平镜像 */
        screenFlip?.let { p.put("screen_flip", it) }
        screenFlipY?.let { p.put("screen_flip_y", it) }
        colMain?.let { p.put("col_main", it) }
        colTrack?.let { p.put("col_track", it) }
        colGrid?.let { p.put("col_grid", it) }
        colRoad?.let { p.put("col_road", it) }
        colCar?.let { p.put("col_car", it) }
        colHint?.let { p.put("col_hint", it) }
        imgOn?.let { p.put("img_on", it) }
        return Json.obj("msg_type" to "SET_CONFIG", "payload" to p).toString()
    }
}

/** 入站报文解析（DEV_STATUS / PONG；未知类型保持原样） */
object InMsg {
    fun msgType(line: String): String =
        runCatching { JSONObject(line).optString("msg_type", "") }.getOrDefault("")

    fun describe(line: String): String = runCatching {
        val o = JSONObject(line)
        val type = o.optString("msg_type", "?")
        val p = o.optJSONObject("payload") ?: JSONObject()
        when (type) {
            "DEV_STATUS" ->
                "DEV_STATUS 亮度=${p.optInt("lcd_brightness")} 速度=${p.optInt("dash_speed")} " +
                    "动画=${p.optBoolean("anim_enable")} 超时=${p.optInt("popup_timeout")}s " +
                    "底衬=${p.optBoolean("scrim_on")}/${p.optInt("scrim_compass")}," +
                    "${p.optInt("scrim_text")},${p.optInt("scrim_route")},${p.optInt("scrim_clock")} " +
                    "网格=${p.optInt("grid_bright")} " +
                    "底图=${p.optBoolean("img_on")} " +
                    "电量=${p.optInt("batt")}%(${p.optInt("vbat")}mV) " +
                    "版本=${p.optString("firmware_ver")} err=${p.optInt("err")}"
            "PONG" -> "PONG ts=${p.optLong("ts")}"
            else -> line
        }
    }.getOrDefault(line)
}
