// RoadGen.kt — road_gen.js 的 Kotlin 移植（手机端路况模板几何生成器，协议 V1.10 / 规格 V0.3）
// 待 Android Studio 工程建好后放入：
//   mobileApp/app/src/main/java/com/espnav/app/nav/roadgen/RoadGen.kt
// 纯逻辑、无 Android 依赖，可加 JUnit 单测（对照 proto/test_road_gen.js 的 18 个断言）。
package com.espnav.app.nav.roadgen

import kotlin.math.*

// ---- 与渲染端共享的几何常量（模板几何规格 V0.3 §5.3 / nav_sim_v2.html RBT）----
object Plane {
    const val CAR_X = 160
    const val CAR_Y = 134          // 车辆屏幕位（南入口/条带近端）
    const val Y_FAR = 20           // 可视远界 y
    object Rbt {                   // O2-A：r=观感外环水平半轴；其余冻结比率
        const val CX = 160; const val CY = 84
        const val R = 58; const val B = 0.40
        const val A_IN = 0.66; const val B_IN = 0.17
    }
}
// 方位 → 椭圆参数角(°)；屏幕系 y 向下顺时针为正
val AZM = mapOf(
    "E" to 0, "SE" to 45, "S" to 90, "SW" to 135,
    "W" to 180, "NW" to 225, "N" to 270, "NE" to 315)

fun clamp(v: Double, lo: Double, hi: Double) = max(lo, min(hi, v))
fun rad(name: String): Double = Math.toRadians((AZM[name] ?: 0).toDouble())

/** 车辆系局部路线(米, x=右 y=前) → 车头朝上屏幕坐标（不做透视，渲染端负责视觉） */
fun screenize(pathM: List<DoubleArray>, pxPerM: Double): List<IntArray> =
    pathM.map { p ->
        intArrayOf(
            round(clamp(Plane.CAR_X + p[0] * pxPerM, 0.0, 319.0)).toInt(),
            round(clamp(Plane.CAR_Y - p[1] * pxPerM, Plane.Y_FAR.toDouble(), 239.0)).toInt())
    }

/** 等距抽稀到 ≤n 点（保持首尾） */
fun decimate(pts: List<IntArray>, n: Int): List<IntArray> {
    if (pts.size <= n) return pts
    val step = (pts.size - 1).toDouble() / (n - 1)
    return (0 until n).map { pts[round(it * step).toInt()] }
}

fun headingNorm(h: Double): Double = ((h % 360) + 360) % 360

// ---- 机动 → 模板类型映射（协议 V1.10 type×dir 组合表）----
data class TypeDir(val type: String, val dir: String? = null)

fun typeForManeuver(m: String, junction: String? = null): TypeDir? = when (m) {
    "turn_left" -> TypeDir(if (junction == "tjunc") "tjunc" else "cross", "left")
    "turn_right" -> TypeDir(if (junction == "tjunc") "tjunc" else "cross", "right")
    "fork_left" -> TypeDir("fork", "left")
    "fork_right" -> TypeDir("fork", "right")
    "curve" -> TypeDir("curve", "left")
    "roundabout" -> TypeDir("roundabout", null)
    else -> null                                   // straight/uturn 不套模板
}

// ---- roundabout 出口计数换算（O1：SDK 播报口径 → dir=exitN）----
fun sdkExitToDir(sdkN: Int, sdkIncludesEntry: Boolean): Int {
    val dirN = sdkN - (if (sdkIncludesEntry) 1 else 0)
    require(dirN >= 1) { "sdkExitToDir: 出口编号非法 sdkN=$sdkN includesEntry=$sdkIncludesEntry" }
    return dirN
}

// ---- roundabout 绿路径 pts（平面坐标；与渲染端方向弧算法同构）----
fun roundaboutPts(exits: List<String>, dirN: Int): List<IntArray> {
    val rb = Plane.Rbt
    val cx = rb.CX; val cy = rb.CY; val r = rb.R.toDouble()
    val b = r * rb.B
    val aIn = r * rb.A_IN; val bIn = r * rb.B_IN
    val exitsL = if (exits.isNotEmpty()) exits else listOf("W", "N", "E")
    val ti = clamp((dirN - 1).toDouble(), 0.0, (exitsL.size - 1).toDouble()).toInt()
    val target = exitsL[ti]
    val thS = rad("S")
    var d0 = rad(exitsL[0]) - thS
    while (d0 > PI) d0 -= 2 * PI
    while (d0 < -PI) d0 += 2 * PI
    val spin = if (d0 >= 0) 1 else -1
    val thT = rad(target)
    val tau = 2 * PI
    var sweep = if (spin > 0) (thT - thS).mod(tau) else -((thS - thT).mod(tau))
    if (sweep == 0.0) sweep = spin * tau * 0.999
    val aM = (r + aIn) / 2; val bM = (b + bIn) / 2
    val n = 32
    val arc = (0..n).map { i ->
        val th = thS + sweep * i / n
        intArrayOf(round(cx + aM * cos(th)).toInt(), round(cy + bM * sin(th)).toInt())
    }
    val outP = intArrayOf(round(cx + r * cos(thT)).toInt(), round(cy + b * sin(thT)).toInt())
    val path = mutableListOf(intArrayOf(Plane.CAR_X, Plane.CAR_Y), intArrayOf(round(cx).toInt(), round(cy + b).toInt()))
    path.addAll(arc); path.add(outP)
    return decimate(path, 16)
}

// ---- NAV_FRAME payload（数据类）----
data class Road(
    val type: String,
    val dir: String? = null,
    val exits: List<String>? = null,
    val pts: List<IntArray>? = null,
    val half: Int = 62)

data class NavInput(
    val heading: Double = 0.0,
    val turnDist: Double = 0.0,
    val hint: String = "",
    val totalDist: Double = 0.0,
    val progressPct: Int = 0,
    val elapsedMin: Int = 0,
    val etaTime: String = "",
    val pathM: List<DoubleArray> = emptyList(),
    val visMeters: Double = 140.0,
    val maneuver: String = "straight",
    val junction: String? = null,
    val rbtExits: List<String>? = null,
    val rbtSdkExit: Int? = null,
    val rbtSdkIncludesEntry: Boolean = false)

data class FramePayload(
    val heading: Int, val turnDist: Int, val hint: String,
    val totalDist: Int, val progressPct: Int, val elapsedMin: Int, val etaTime: String,
    val pos: IntArray,
    val road: Road? = null,
    val centerLine: List<IntArray>? = null,
    val pastCenter: List<IntArray>? = null,
    val routeCenter: List<IntArray>? = null)

/** 组装 NAV_FRAME payload（核心入口；语义同 road_gen.js buildFrame） */
fun buildFrame(nav: NavInput): FramePayload {
    val px = (Plane.CAR_Y - Plane.Y_FAR) / max(1.0, nav.visMeters)
    val base = FramePayload(
        heading = headingNorm(nav.heading).roundToInt(),
        turnDist = nav.turnDist.roundToInt(),
        hint = nav.hint,
        totalDist = nav.totalDist.roundToInt(),
        progressPct = nav.progressPct,
        elapsedMin = nav.elapsedMin,
        etaTime = nav.etaTime,
        pos = intArrayOf(Plane.CAR_X, Plane.CAR_Y - 24))
    val m = nav.maneuver
    val entering = nav.turnDist < 200 && m != "straight" && m != "uturn"
    return if (entering) {
        val tm = typeForManeuver(m, nav.junction)!!
        val road = if (m == "roundabout") {
            val exits = nav.rbtExits ?: listOf("W", "N", "E")
            val dirN = sdkExitToDir(nav.rbtSdkExit ?: 1, nav.rbtSdkIncludesEntry)
            Road(type = "roundabout", dir = "exit$dirN", exits = exits, pts = roundaboutPts(exits, dirN))
        } else {
            val pts = decimate(screenize(nav.pathM.ifEmpty { listOf(doubleArrayOf(0.0, 8.0)) }, px), 16)
            Road(type = tm.type, dir = tm.dir, pts = pts)
        }
        base.copy(road = road, centerLine = road.pts)
    } else {
        val c = screenize(nav.pathM.ifEmpty {
            listOf(doubleArrayOf(0.0, 4.0), doubleArrayOf(0.0, 60.0), doubleArrayOf(0.0, 130.0))
        }, px)
        val center = decimate(c, 6)
        base.copy(centerLine = center, pastCenter = listOf(center.first()), routeCenter = center.drop(1))
    }
}
