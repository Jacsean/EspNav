package com.espnav.app.data

import kotlin.math.abs
import kotlin.math.acos
import kotlin.math.cos
import kotlin.math.sqrt

/**
 * 行程图（小地图）折线采样器：把高德返回的原始路径（数百~数千点）压到协议允许的点数
 * （[MAX_N] = 16），同时**优先保住几何上重要的点**。
 *
 * 为什么不用等间隔采样（原 `AmapNavSource.downsample()`）：
 *  - 长直段与弯道一律等分 → 点数被直路"吃掉"，拐弯处点数不足；
 *  - 拐弯恰好落在两个采样点之间时，**整个路口会从行程图上消失**（"抄近道"）。
 *
 * 为什么不能用"抽拐点"（转角阈值法）：
 *  一条很长、弧度均匀的弧线（大半径弯道/匝道）中间没有任何"尖点"，抽不出点，
 *  会被两端点连成的直线抹平 —— 故采用按**几何偏差**分配点数的算法。
 *
 * 本文件提供两种算法（均工作在**米制**坐标，已含 cos(纬度) 折算）：
 *
 *  - [simplifyVw] Visvalingam-Whyatt 面积删点 + **直角拐点锁定**（默认）
 *      面积 = ½·弦长·弧高 ⇒ 长直段（面积 0）最先被删；**长而缓的弧线（弦长很大）面积天然
 *      很大 → 自动保留**；直角路口被 [LOCK_DEG] 锁定，不会因 VW 的固有弱点被剪掉。
 *  - [simplifyDp] 自适应 ε 的道格拉斯-普克（二分 ε 直到点数达标）
 *      误差有硬界，但起点敏感、长缓弧在极端压缩下可能被拉直。
 *
 * 两者都保证：结果 ≤ maxN 点，且**必含首尾点**（起点/终点不会被删）。
 */
object PolylineSampler {

    /** 米制平面点（x = 东向米、y = 北向米） */
    data class Pt(val x: Double, val y: Double)

    /** 采样方式标识（与 AppPrefs.samplerMode 的取值一致） */
    const val MODE_VW = "vw"
    const val MODE_DP = "dp"

    /** 协议单帧点上限（对应固件 nav_frame.h 的 NAV_MAX_PTS） */
    const val MAX_N = 16

    /** 直角锁定阈值（度）：转角 >= 该值且两侧线段都不短 → 视为语义拐点，不参与删点 */
    private const val LOCK_DEG = 45.0

    /** 自适应 ε 的二分次数（40 次足以把区间收窄到任意实际精度） */
    private const val DP_BISECT = 40

    private const val M_PER_LAT = 111_320.0

    // ---------------- 对外入口 ----------------

    /** 按 [mode] 选择算法；[minLegM] 仅 VW 的直角锁定使用（图上约 2px 对应的米数） */
    fun simplify(mode: String, pts: List<Pt>, maxN: Int = MAX_N, minLegM: Double = 0.0): List<Pt> =
        when (mode) {
            MODE_DP -> simplifyDp(pts, maxN)
            else -> simplifyVw(pts, maxN, minLegM)
        }

    /**
     * 经纬度路径 -> 米制（以 [origin] 为原点）。
     * 含 cos(纬度) 折算：不做的话东西向距离会被高估，行程图横向被拉伸（北京纬度下约 30%）。
     */
    fun toMeters(pts: List<GeoPoint>, origin: GeoPoint): List<Pt> {
        val mPerLon = M_PER_LAT * cos(Math.toRadians(origin.lat))
        return pts.map { Pt((it.lon - origin.lon) * mPerLon, (it.lat - origin.lat) * M_PER_LAT) }
    }

    /** 米制 -> 经纬度（[toMeters] 的逆变换） */
    fun toGeo(pts: List<Pt>, origin: GeoPoint): List<GeoPoint> {
        val mPerLon = M_PER_LAT * cos(Math.toRadians(origin.lat))
        return pts.map { GeoPoint(origin.lat + it.y / M_PER_LAT, origin.lon + it.x / mPerLon) }
    }

    /**
     * 行程图专用入口：经纬度路径 -> 采样后的经纬度路径（<= maxN 点）。
     *
     * 内部折算成米制再采样，因此直角锁定的长度门槛可以是"行程图上约 2px 对应的米数"
     * （由 [minLegMeters] 按路线跨度与 OV_SPAN 估算），避免把细碎拐点锁死。
     */
    fun sampleGeo(pts: List<GeoPoint>, mode: String, maxN: Int = MAX_N): List<GeoPoint> {
        if (pts.size <= maxN) return pts
        val origin = pts.first()
        val meters = toMeters(pts, origin)
        return toGeo(simplify(mode, meters, maxN, minLegMeters(meters)), origin)
    }

    /** 直角锁定的长度门槛 = 行程图上约 2px 对应的米数 */
    private fun minLegMeters(meters: List<Pt>): Double {
        var x0 = Double.MAX_VALUE
        var x1 = -Double.MAX_VALUE
        var y0 = Double.MAX_VALUE
        var y1 = -Double.MAX_VALUE
        for (p in meters) {
            if (p.x < x0) x0 = p.x
            if (p.x > x1) x1 = p.x
            if (p.y < y0) y0 = p.y
            if (p.y > y1) y1 = p.y
        }
        val span = maxOf(x1 - x0, y1 - y0).coerceAtLeast(1e-9)
        val metersPerUnit = span / (NavStateMapper.OV_SPAN - 4.0)
        val pxPerUnit = 80.0 / NavStateMapper.OV_SPAN      // 固件画布 80px 显示 OV_SPAN 个单位
        return 2.0 / pxPerUnit * metersPerUnit
    }

    // ---------------- A. Visvalingam-Whyatt + 直角锁定 ----------------

    /**
     * VW 面积删点。用双向链表 + 每轮全局扫描找最小面积点（O(n²)，n≈2000 时实测毫秒级）。
     *
     * @param minLegM 直角锁定的"两侧线段最小长度"（米）；<=0 表示不设长度门槛
     */
    fun simplifyVw(pts: List<Pt>, maxN: Int = MAX_N, minLegM: Double = 0.0): List<Pt> {
        val n = pts.size
        if (n <= maxN || maxN < 2) return pts

        val alive = BooleanArray(n) { true }
        val next = IntArray(n) { it + 1 }
        val prev = IntArray(n) { it - 1 }

        /* ① 首尾锁定：起点/终点永不删除 */
        val locked = BooleanArray(n)
        locked[0] = true
        locked[n - 1] = true

        /* ② 直角锁定：转角足够大且两侧线段够长 → 语义拐点（VW 会误剪这些窄三角形） */
        for (i in 1 until n - 1) {
            if (turnDeg(pts[i - 1], pts[i], pts[i + 1]) >= LOCK_DEG &&
                dist(pts[i - 1], pts[i]) >= minLegM &&
                dist(pts[i], pts[i + 1]) >= minLegM
            ) {
                locked[i] = true
            }
        }

        /* ③ 反复删除面积最小的可删点（每删一点，其邻居面积会变化 → 必须重算） */
        var count = n
        while (count > maxN) {
            var best = -1
            var bestArea = Double.MAX_VALUE
            var i = next[0]
            while (i != n - 1) {                       // 只遍历内部点（跳过首尾）
                if (!locked[i]) {
                    val a = triArea(pts[prev[i]], pts[i], pts[next[i]])
                    if (a < bestArea) { bestArea = a; best = i }
                }
                i = next[i]
            }
            if (best < 0) break                        // 剩余点全被锁定，无法再删
            alive[best] = false
            next[prev[best]] = next[best]
            prev[next[best]] = prev[best]
            count--
        }

        return pts.filterIndexed { i, _ -> alive[i] }
    }

    // ---------------- B. 自适应 ε 的道格拉斯-普克 ----------------

    /** 二分 ε，使 DP 输出点数 <= maxN；返回该 ε 下的结果 */
    fun simplifyDp(pts: List<Pt>, maxN: Int = MAX_N): List<Pt> {
        val n = pts.size
        if (n <= maxN || maxN < 2) return pts

        var lo = 0.0
        var hi = bboxDiag(pts)
        repeat(DP_BISECT) {
            val mid = (lo + hi) * 0.5
            if (dpKeep(pts, mid).size > maxN) lo = mid else hi = mid
        }
        return dpKeep(pts, hi)
    }

    /** 迭代式 DP（显式栈，避免大 n 时递归爆栈） */
    private fun dpKeep(pts: List<Pt>, eps: Double): List<Pt> {
        val n = pts.size
        val keep = BooleanArray(n)
        keep[0] = true
        keep[n - 1] = true
        val stack = ArrayDeque<IntArray>()
        stack.addLast(intArrayOf(0, n - 1))
        while (stack.isNotEmpty()) {
            val seg = stack.removeLast()
            val i0 = seg[0]
            val i1 = seg[1]
            if (i1 - i0 < 2) continue
            var k = -1
            var dmax = 0.0
            for (i in i0 + 1 until i1) {
                val d = pointSegDist(pts[i], pts[i0], pts[i1])
                if (d > dmax) { dmax = d; k = i }
            }
            if (k >= 0 && dmax > eps) {
                keep[k] = true
                stack.addLast(intArrayOf(i0, k))
                stack.addLast(intArrayOf(k, i1))
            }
        }
        return pts.filterIndexed { i, _ -> keep[i] }
    }

    // ---------------- 几何工具 ----------------

    /** 三角形有效面积 = ½·|(b-a)×(c-b)| = ½·弦长·弧高 */
    private fun triArea(a: Pt, b: Pt, c: Pt): Double {
        val ux = b.x - a.x
        val uy = b.y - a.y
        val vx = c.x - b.x
        val vy = c.y - b.y
        return 0.5 * abs(ux * vy - uy * vx)
    }

    /** 方向变化角（度）：直行 = 0，直角转弯 = 90，掉头 = 180 */
    private fun turnDeg(a: Pt, b: Pt, c: Pt): Double {
        val v1x = b.x - a.x
        val v1y = b.y - a.y
        val v2x = c.x - b.x
        val v2y = c.y - b.y
        val l1 = sqrt(v1x * v1x + v1y * v1y)
        val l2 = sqrt(v2x * v2x + v2y * v2y)
        if (l1 < 1e-9 || l2 < 1e-9) return 0.0
        var cv = (v1x * v2x + v1y * v2y) / (l1 * l2)
        if (cv > 1.0) cv = 1.0
        if (cv < -1.0) cv = -1.0
        return Math.toDegrees(acos(cv))
    }

    private fun dist(a: Pt, b: Pt): Double {
        val dx = b.x - a.x
        val dy = b.y - a.y
        return sqrt(dx * dx + dy * dy)
    }

    /** 点到线段 a-b 的距离（线退化为点时即为点距） */
    private fun pointSegDist(p: Pt, a: Pt, b: Pt): Double {
        val dx = b.x - a.x
        val dy = b.y - a.y
        val len2 = dx * dx + dy * dy
        if (len2 < 1e-12) return dist(p, a)
        var t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2
        if (t < 0.0) t = 0.0
        if (t > 1.0) t = 1.0
        val ex = p.x - (a.x + t * dx)
        val ey = p.y - (a.y + t * dy)
        return sqrt(ex * ex + ey * ey)
    }

    private fun bboxDiag(pts: List<Pt>): Double {
        var x0 = Double.MAX_VALUE
        var x1 = -Double.MAX_VALUE
        var y0 = Double.MAX_VALUE
        var y1 = -Double.MAX_VALUE
        for (p in pts) {
            if (p.x < x0) x0 = p.x
            if (p.x > x1) x1 = p.x
            if (p.y < y0) y0 = p.y
            if (p.y > y1) y1 = p.y
        }
        return sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0))
    }
}
