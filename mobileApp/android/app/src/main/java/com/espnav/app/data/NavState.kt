package com.espnav.app.data

/** 下一步动作类型（高德导航回调 / 模拟器统一产出） */
enum class TurnType {
    STRAIGHT, SLIGHT_LEFT, LEFT, SHARP_LEFT, SLIGHT_RIGHT, RIGHT, SHARP_RIGHT,
    UTURN, ROUNDABOUT, MERGE, FORK_LEFT, FORK_RIGHT, ARRIVE, UNKNOWN
}

/** 路况模板（对应固件 road.type，协议 §3.1.1） */
enum class RoadType(val wire: String) {
    STRAIGHT("straight"),
    CURVE("curve"),
    TJUNC("tjunc"),
    CROSS("cross"),
    MULTI("multi"),
    ROUNDABOUT("roundabout"),
    FORK("fork")
}

/** 经纬度点（高德导航 SDK 回调用） */
data class GeoPoint(val lat: Double, val lon: Double)

/**
 * 导航状态快照 —— App 内部统一模型。
 *
 * 数据来源可以是：
 *  - 模拟器 MockNavigator（当前 P1/P3 联调用）
 *  - 高德导航 SDK 回调（P3 接入后）
 * 两者都产出 NavState，再经 NavStateMapper 转成协议帧 NAV_FRAME，因此**屏幕端无需任何改动**。
 */
data class NavState(
    /** 下一步动作 */
    val turnType: TurnType = TurnType.STRAIGHT,
    /** 到下一步动作的距离（米） */
    val turnDistMeters: Int = 0,
    /** 当前道路名（可选，显示在提示里） */
    val currentRoad: String = "",
    /** 动作后进入的道路名（可选） */
    val nextRoad: String = "",

    /** 全程 / 剩余 距离（米） */
    val totalDistMeters: Int = 0,
    val remainDistMeters: Int = 0,
    /** 已用时（秒）与预计到达（"14:27"） */
    val elapsedSec: Int = 0,
    val etaText: String = "",

    /** 车头朝向（度，0=北，顺时针） */
    val headingDeg: Int = 0,
    val speedKmh: Int = 0,

    /** 显式指定路况模板（模拟器用；高德接入时留空由 turnType 推断） */
    val roadTypeHint: RoadType? = null,

    /** 环岛出口序号（1 起）与出口方位（"W"/"N"/"E"…） */
    val exitNumber: Int = 0,
    val exitDirections: List<String> = emptyList(),

    /* ---- 已投影到屏幕/小地图坐标的路径（由 NavStateMapper.project / miniMap 生成）---- */
    val passedPath: List<Pair<Int, Int>> = emptyList(),
    val remainPath: List<Pair<Int, Int>> = emptyList(),
    val overviewPath: List<Pair<Int, Int>> = emptyList()
)
