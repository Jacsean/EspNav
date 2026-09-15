package com.espnav.app.data

import com.espnav.app.protocol.NavFrame
import com.espnav.app.protocol.RoadSpec

/**
 * P1 模拟导航数据源：按"直行 → 弯道 → T口 → 十字 → 环岛 → 多岔 → 匝道"循环推进，
 * 生成与 HTML V2 / 固件模板参数一致的 NAV_FRAME 序列（默认 5 Hz）。
 * 后续 P3 由高德导航 SDK 的真实数据替换本类。
 */
class MockNavigator(private val totalDistMeters: Int = 8000) {

    private data class Stage(
        val name: String,
        val enterDist: Int,                       // 进入本阶段时的剩余距离（米）
        val hint: (Int) -> String,
        val heading: Int,
        val road: RoadSpec?
    )

    private val stages = listOf(
        Stage("直行", 800, { d -> "前方${d}米直行" }, 0,
            RoadSpec(type = "straight", pts = listOf(160 to 144, 160 to 30), half = 62)),
        Stage("弯道左", 500, { d -> "前方弯道${d}米向左" }, 315,
            RoadSpec(type = "curve",
                pts = listOf(160 to 144, 160 to 112, 150 to 82, 118 to 52, 96 to 30), half = 62)),
        Stage("T口左转", 320, { d -> "前方路口左转 ${d}米" }, 270,
            RoadSpec(type = "tjunc", dir = "left",
                pts = listOf(160 to 144, 160 to 70, 32 to 70), half = 62)),
        Stage("十字右转", 240, { d -> "十字路口右转 ${d}米" }, 45,
            RoadSpec(type = "cross", dir = "right",
                pts = listOf(160 to 144, 160 to 70, 300 to 70), half = 62)),
        Stage("环岛第2出口", 200, { d -> "前方环岛第2出口驶出 ${d}米" }, 0,
            RoadSpec(type = "roundabout", dir = "exit2", exits = listOf("W", "N", "E"))),
        Stage("多岔", 140, { d -> "多岔路口走左侧支路 ${d}米" }, 315,
            RoadSpec(type = "multi",
                pts = listOf(160 to 144, 160 to 68, 110 to 40), half = 62)),
        Stage("匝道右", 100, { d -> "靠右驶出匝道 ${d}米" }, 45,
            RoadSpec(type = "fork", dir = "right",
                pts = listOf(160 to 100, 180 to 78, 214 to 50, 252 to 32), half = 62))
    )

    private var stageIdx = 0
    private var travelled = 0          // 已行驶（米）
    private var elapsedSec = 0

    /** 轨迹（行程图）：真实走过的路径点，最多保留 16 个 */
    private val track = ArrayList<Pair<Int, Int>>()
    private val trackAll = listOf(
        8 to 6, 12 to 10, 17 to 13, 22 to 17, 26 to 21,
        30 to 17, 33 to 13, 36 to 9, 30 to 12, 25 to 16,
        21 to 20, 24 to 24, 28 to 28, 32 to 32, 36 to 36, 39 to 39
    )

    fun reset() {
        stageIdx = 0
        travelled = 0
        elapsedSec = 0
        track.clear()
    }

    /** 生成下一帧；distStep = 每次推进的米数（20 米 ≈ 200ms 一帧 ≈ 100km/h 上限） */
    fun next(distStep: Int = 20): NavFrame {
        val stage = stages[stageIdx]
        travelled += distStep
        elapsedSec += 1

        // 行程图轨迹推进（每 2 帧一个点）
        if (elapsedSec % 2 == 0 && track.size < 16) {
            val idx = (track.size) % trackAll.size
            track.add(trackAll[idx])
        }

        val remainInStage = (stage.enterDist - (travelled - stageStart(stageIdx))).coerceAtLeast(0)
        if (remainInStage <= 0) {
            stageIdx = (stageIdx + 1) % stages.size
        }

        return buildFrame(stage, remainInStage)
    }

    private fun stageStart(idx: Int): Int = stages.take(idx).sumOf { it.enterDist }

    private fun buildFrame(stage: Stage, remainInStage: Int): NavFrame {
        val center = listOf(160 to 144, 160 to 118, 160 to 86, 160 to 54, 160 to 30)
        val progress = (travelled * 100 / totalDistMeters).coerceIn(0, 99)
        // 已走过/未走过分段：随进度推进（保持协议原点在近端 144）
        val cut = (1 + progress / 25).coerceIn(1, center.size - 1)

        return NavFrame(
            heading = (stage.heading + 360) % 360,
            turnDist = remainInStage,
            hint = stage.hint(remainInStage),
            totalDist = totalDistMeters,
            progressPct = progress,
            elapsedMin = elapsedSec / 60,
            etaTime = "14:27",
            centerLine = center,
            pastCenter = center.subList(0, cut),
            routeCenter = center.subList(cut, center.size),
            pos = 160 to 110,
            overview = ArrayList(track),
            overviewDot = track.lastOrNull(),
            road = stage.road
        )
    }

    fun stageName(): String = stages[stageIdx].name
}
