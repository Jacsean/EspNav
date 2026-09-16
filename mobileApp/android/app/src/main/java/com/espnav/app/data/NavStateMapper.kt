package com.espnav.app.data

import com.espnav.app.protocol.NavFrame
import com.espnav.app.protocol.RoadSpec
import kotlin.math.cos
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * 导航状态 -> 协议帧（V1.10）映射层。
 *
 * 屏幕几何与固件/HTML V2 冻结一致：320x240；路面近端 y=144、远端 y=30；
 * 车标 pos 固定 (160,110)；主路半宽 62、支路半宽 35。
 * 小地图（overview）用"局部坐标 0..40，y 向上"，y 翻转由固件完成。
 */
object NavStateMapper {

    const val W = 320
    const val H = 240
    const val NEAR_Y = 144
    const val FAR_Y = 30
    const val CX = 160
    const val CAR_Y = 110            // 车标固定（协议：pos 固定）
    const val TPL_MAIN_HALF = 62
    const val TPL_SIDE_HALF = 35
    const val OV_SPAN = 40           // 小地图局部坐标跨度

    /** 固件字库支持的汉字（font_data.c 子集）。提示语只用这些字 + ASCII，避免屏幕出现方块。 */
    private const val SUPPORTED_HANZI =
        "前方米直行左右转路口弯道岛第出驶多岔走侧支靠匝道十字继续掉头进入离开距" +
            "离到达预计全程已完成耗时信号中断微短信张三来电未接方向北南东西环主辅并" +
            "线段起点终剩余分钟小时公设置亮度动画速度弹窗消息关闭清除暂无数据连接成" +
            "功失败重试等待初始化错误状态电量充电导航开始结束取消确认返回菜单首页图" +
            "路线规划偏航重算拥堵缓行事故施工限速拍照测速服务区收费站桥梁隧道高速国" +
            "道省县乡上下一二三四五六七八九十百千万零地点老记得准时过来个取件码稍后" +
            "发材料明天早晚上"

    /** 去掉字库不支持的汉字（宁可提示更短，也不要在屏幕上显示方块） */
    fun safe(text: String): String {
        val sb = StringBuilder()
        for (ch in text) {
            if (ch.code < 128 || SUPPORTED_HANZI.indexOf(ch) >= 0) sb.append(ch)
        }
        return sb.toString().trim()
    }

    // ---------------- 提示语 ----------------

    private fun actionText(s: NavState): String = when (s.turnType) {
        TurnType.STRAIGHT -> "直行"
        TurnType.SLIGHT_LEFT -> "靠左"
        TurnType.LEFT -> "左转"
        TurnType.SHARP_LEFT -> "左转"
        TurnType.SLIGHT_RIGHT -> "靠右"
        TurnType.RIGHT -> "右转"
        TurnType.SHARP_RIGHT -> "右转"
        TurnType.UTURN -> "掉头"
        TurnType.MERGE -> "并线"
        TurnType.FORK_LEFT -> "靠左驶出"
        TurnType.FORK_RIGHT -> "靠右驶出"
        TurnType.ROUNDABOUT -> if (s.exitNumber > 0) "第${s.exitNumber}出口驶出" else "环岛驶出"
        TurnType.ARRIVE -> "到达终点"
        TurnType.UNKNOWN -> "继续直行"
    }

    fun hintOf(s: NavState): String {
        val act = actionText(s)
        val head = when {
            s.turnType == TurnType.ARRIVE -> ""
            s.turnDistMeters <= 0 -> "前方"
            else -> "前方${s.turnDistMeters}米"
        }
        return safe(head + act)
    }

    // ---------------- 路况模板 ----------------

    fun roadOf(s: NavState): RoadSpec? {
        s.roadTypeHint?.let { return specFor(it, s) }
        return when (s.turnType) {
            TurnType.STRAIGHT, TurnType.UNKNOWN -> specFor(RoadType.STRAIGHT, s)
            TurnType.SLIGHT_LEFT, TurnType.LEFT, TurnType.SHARP_LEFT -> specFor(RoadType.CURVE, s)
            TurnType.SLIGHT_RIGHT, TurnType.RIGHT, TurnType.SHARP_RIGHT -> specFor(RoadType.CURVE, s)
            TurnType.ROUNDABOUT -> specFor(RoadType.ROUNDABOUT, s)
            TurnType.FORK_LEFT, TurnType.FORK_RIGHT, TurnType.MERGE -> specFor(RoadType.FORK, s)
            TurnType.UTURN, TurnType.ARRIVE -> null
        }
    }

    private fun isLeft(s: NavState): Boolean = when (s.turnType) {
        TurnType.LEFT, TurnType.SHARP_LEFT, TurnType.SLIGHT_LEFT,
        TurnType.FORK_LEFT, TurnType.MERGE -> true
        else -> false
    }

    private fun specFor(type: RoadType, s: NavState): RoadSpec = when (type) {
        RoadType.STRAIGHT -> RoadSpec(
            type = "straight",
            pts = listOf(CX to NEAR_Y, CX to 118, CX to 86, CX to 54, CX to FAR_Y),
            half = TPL_MAIN_HALF
        )
        RoadType.CURVE -> RoadSpec(
            type = "curve",
            dir = if (isLeft(s)) "left" else "right",
            pts = if (isLeft(s)) listOf(CX to NEAR_Y, CX to 112, 150 to 82, 118 to 52, 96 to FAR_Y)
            else listOf(CX to NEAR_Y, CX to 112, 172 to 82, 204 to 52, 226 to FAR_Y),
            half = TPL_MAIN_HALF
        )
        RoadType.TJUNC -> RoadSpec(
            type = "tjunc",
            dir = if (isLeft(s)) "left" else "right",
            pts = if (isLeft(s)) listOf(CX to NEAR_Y, CX to 70, 32 to 70)
            else listOf(CX to NEAR_Y, CX to 70, 288 to 70),
            half = TPL_MAIN_HALF
        )
        RoadType.CROSS -> RoadSpec(
            type = "cross",
            dir = if (isLeft(s)) "left" else "right",
            pts = listOf(CX to NEAR_Y, CX to 70, (if (isLeft(s)) 32 else 288) to 70),
            half = TPL_MAIN_HALF
        )
        RoadType.MULTI -> RoadSpec(
            type = "multi",
            pts = listOf(CX to NEAR_Y, CX to 68, 110 to 40),
            half = TPL_MAIN_HALF
        )
        RoadType.ROUNDABOUT -> RoadSpec(
            type = "roundabout",
            dir = if (s.exitNumber > 0) "exit${s.exitNumber}" else "exit1",
            exits = s.exitDirections.ifEmpty { listOf("W", "N", "E") }
        )
        RoadType.FORK -> RoadSpec(
            type = "fork",
            dir = if (isLeft(s)) "left" else "right",
            pts = if (isLeft(s)) listOf(160 to 100, 140 to 78, 106 to 50, 68 to 32)
            else listOf(160 to 100, 180 to 78, 214 to 50, 252 to 32),
            half = TPL_MAIN_HALF
        )
    }

    // ---------------- 路径投影 ----------------

    /**
     * 经纬度路径 -> "车头朝上"的屏幕坐标。
     * @param origin 当前车辆位置（会映射到屏幕中下部 anchorY 处）
     * @param headingDeg 车头朝向（0=北，顺时针）
     * @param pxPerMeter 缩放（像素/米），越大越"放大"
     */
    fun project(
        pts: List<GeoPoint>,
        origin: GeoPoint,
        headingDeg: Int,
        pxPerMeter: Double = 0.9,
        anchorY: Int = NEAR_Y
    ): List<Pair<Int, Int>> {
        if (pts.isEmpty()) return emptyList()
        val mPerLat = 111_320.0
        val mPerLon = 111_320.0 * cos(Math.toRadians(origin.lat))
        val rad = Math.toRadians(-headingDeg.toDouble())
        val cs = cos(rad)
        val sn = sin(rad)
        return pts.map { p ->
            val east = (p.lon - origin.lon) * mPerLon       // 东向（米）
            val north = (p.lat - origin.lat) * mPerLat      // 北向（米）
            val xr = east * cs - north * sn
            val yr = east * sn + north * cs
            val sx = CX + (xr * pxPerMeter).roundToInt()
            val sy = anchorY - (yr * pxPerMeter).roundToInt()   // 前方（北）朝屏幕上方
            sx.coerceIn(0, W - 1) to sy.coerceIn(0, H - 1)
        }
    }

    /**
     * 经纬度路径 -> 小地图局部坐标（0..40，y 向上；固件绘制时做 40-y 翻转）。
     * 按路径包围盒**自适应缩放**并居中，保证整条路线都能画进小地图（北在上）。
     */
    fun miniMapFromGeo(pts: List<GeoPoint>): List<Pair<Int, Int>> {
        if (pts.isEmpty()) return emptyList()
        val latMin = pts.minOf { it.lat }
        val latMax = pts.maxOf { it.lat }
        val lonMin = pts.minOf { it.lon }
        val lonMax = pts.maxOf { it.lon }
        val latSpan = (latMax - latMin).coerceAtLeast(1e-6)
        val lonSpan = (lonMax - lonMin).coerceAtLeast(1e-6)
        val scale = (OV_SPAN - 4.0) / maxOf(latSpan, lonSpan)     // 留 2 单位边距
        return pts.map { p ->
            val x = ((p.lon - lonMin) * scale + 2.0).roundToInt().coerceIn(0, OV_SPAN)
            val y = ((p.lat - latMin) * scale + 2.0).roundToInt().coerceIn(0, OV_SPAN)  // 北在上
            x to y
        }
    }

    /** 屏幕路径 -> 小地图局部坐标（0..40，y 向上；固件绘制时会做 40-y 翻转） */
    fun miniMap(pts: List<Pair<Int, Int>>): List<Pair<Int, Int>> =
        pts.take(16).map { (x, y) ->
            val mx = (x * OV_SPAN / W).coerceIn(0, OV_SPAN)
            val my = ((H - y) * OV_SPAN / H).coerceIn(0, OV_SPAN)
            mx to my
        }

    // ---------------- 组装协议帧 ----------------

    fun toFrame(s: NavState): NavFrame {
        val total = if (s.totalDistMeters > 0) s.totalDistMeters else 0
        val progress = if (total > 0) (((total - s.remainDistMeters) * 100) / total).coerceIn(0, 99) else 0
        val center = s.remainPath.ifEmpty {
            listOf(CX to NEAR_Y, CX to 118, CX to 86, CX to 54, CX to FAR_Y)
        }
        val cut = if (center.size <= 1) 1 else (1 + progress / 25).coerceIn(1, center.size - 1)
        val mini = s.overviewPath.ifEmpty { miniMap(center) }
        return NavFrame(
            heading = ((s.headingDeg % 360) + 360) % 360,
            turnDist = s.turnDistMeters.coerceAtLeast(0),
            hint = hintOf(s),
            totalDist = total,
            progressPct = progress,
            elapsedMin = s.elapsedSec / 60,
            etaTime = s.etaText,
            centerLine = center,
            pastCenter = s.passedPath.ifEmpty { center.subList(0, cut) },
            routeCenter = center.subList(cut, center.size),
            pos = CX to CAR_Y,
            overview = mini,
            overviewDot = mini.lastOrNull(),
            road = roadOf(s)
        )
    }
}
