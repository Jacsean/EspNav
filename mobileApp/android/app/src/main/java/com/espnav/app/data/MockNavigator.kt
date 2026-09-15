package com.espnav.app.data

/**
 * 模拟导航数据源：产出统一的 NavState（再由 NavStateMapper 转成协议帧 NAV_FRAME）。
 * 高德导航 SDK 接入后，由真实导航回调替换本类，屏幕端无需改动。
 */
class MockNavigator(private val totalDistMeters: Int = 8000) {

    private data class Stage(
        val name: String,
        val enterDist: Int,
        val turn: TurnType,
        val road: RoadType,
        val exitNumber: Int = 0,
        val heading: Int = 0
    )

    private val stages = listOf(
        Stage("直行", 800, TurnType.STRAIGHT, RoadType.STRAIGHT, heading = 0),
        Stage("弯道左", 500, TurnType.LEFT, RoadType.CURVE, heading = 315),
        Stage("T口左转", 320, TurnType.LEFT, RoadType.TJUNC, heading = 270),
        Stage("十字右转", 240, TurnType.RIGHT, RoadType.CROSS, heading = 45),
        Stage("环岛第2出口", 200, TurnType.ROUNDABOUT, RoadType.ROUNDABOUT, exitNumber = 2, heading = 0),
        Stage("多岔", 140, TurnType.SLIGHT_LEFT, RoadType.MULTI, heading = 315),
        Stage("匝道右", 100, TurnType.FORK_RIGHT, RoadType.FORK, heading = 45)
    )

    private var idx = 0
    private var travelled = 0
    private var elapsedSec = 0

    fun reset() {
        idx = 0
        travelled = 0
        elapsedSec = 0
    }

    fun stageName(): String = stages[idx].name

    /** 生成下一个导航状态（distStep = 每次推进的米数，20 米约等于 200ms 一帧） */
    fun next(distStep: Int = 20): NavState {
        val st = stages[idx]
        travelled += distStep
        elapsedSec += 1

        val start = stages.take(idx).sumOf { it.enterDist }
        val remainInStage = (st.enterDist - (travelled - start)).coerceAtLeast(0)
        if (remainInStage <= 0) idx = (idx + 1) % stages.size

        return NavState(
            turnType = st.turn,
            turnDistMeters = remainInStage,
            totalDistMeters = totalDistMeters,
            remainDistMeters = (totalDistMeters - travelled).coerceAtLeast(0),
            elapsedSec = elapsedSec,
            etaText = "14:27",
            headingDeg = (st.heading + 360) % 360,
            roadTypeHint = st.road,
            exitNumber = st.exitNumber,
            exitDirections = if (st.road == RoadType.ROUNDABOUT) listOf("W", "N", "E") else emptyList(),
            passedPath = listOf(160 to 144, 160 to 122),
            remainPath = pathFor(st),
            overviewPath = emptyList()
        )
    }

    /** 与路况模板配套的“剩余路径”（屏幕坐标，用于绿色路径线） */
    private fun pathFor(st: Stage): List<Pair<Int, Int>> = when (st.road) {
        RoadType.STRAIGHT -> listOf(160 to 144, 160 to 118, 160 to 86, 160 to 54, 160 to 30)
        RoadType.CURVE ->
            if (st.turn == TurnType.LEFT || st.turn == TurnType.SLIGHT_LEFT)
                listOf(160 to 144, 160 to 112, 150 to 82, 118 to 52, 96 to 30)
            else
                listOf(160 to 144, 160 to 112, 172 to 82, 204 to 52, 226 to 30)
        RoadType.TJUNC -> listOf(160 to 144, 160 to 104, 160 to 78, 120 to 70, 60 to 70)
        RoadType.CROSS -> listOf(160 to 144, 160 to 104, 160 to 78, 200 to 70, 260 to 70)
        RoadType.ROUNDABOUT -> listOf(160 to 144, 160 to 118, 160 to 96, 148 to 88, 132 to 92)
        RoadType.MULTI -> listOf(160 to 144, 160 to 104, 152 to 78, 120 to 56, 96 to 40)
        RoadType.FORK -> listOf(160 to 144, 160 to 118, 160 to 100, 180 to 78, 214 to 50, 252 to 32)
    }
}
