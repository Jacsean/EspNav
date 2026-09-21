package com.espnav.app.ui

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PointF
import android.util.AttributeSet
import android.view.View
import com.espnav.app.data.GeoPoint
import kotlin.math.cos

/**
 * 导航态右下角的「行程图」卡片（与 ESP 屏右下角行程图对应）。
 *
 * 画法：整条路线缩略 —— 北在上、等比缩放到正方形内，x 方向按 cos(纬度) 折算
 * （与 App 的 OverviewProjector、固件 draw_overview 同一套做法，避免东西向被拉伸）。
 * 已走段灰色、未走段蓝色、起点绿、终点红、当前位置黄点。
 *
 * 【M1.5】背景改为**半透明**并在其上叠**暗色网格**（10dp 次格线 / 每 5 格更亮一档），
 * 与 ESP 端行程图（半透明暗底衬 + 网格）观感一致 —— 用户 2026-09-19 要求。
 */
class TripOverviewView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    private companion object {
        /** 绘制降采样上限（整条路线可能上千点，每帧全画会掉帧） */
        const val DRAW_MAX = 220
        const val PAD = 6f
        const val CORNER = 10f
    }

    private var path: List<GeoPoint> = emptyList()
    private var curIndex = 0

    /* 每次 onDraw 前算好的投影参数（供 toPx 复用） */
    private var latMax = 0.0
    private var lonMin = 0.0
    private var kx = 1.0
    private var scale = 1.0
    private var ox = 0f
    private var oy = 0f

    /** 背景：半透明黑（60%），能看到底下地图 —— 取代此前的 88% 不透明 */
    private val paintBg = Paint().apply { color = 0x99000000.toInt() }
    private val paintBorder = Paint().apply {
        color = 0x662E7D32
        strokeWidth = 1.5f
        style = Paint.Style.STROKE
    }
    /** 暗色网格：次格线（每 10dp） */
    private val paintGridMinor = Paint().apply {
        color = 0x2A9E9E9E.toInt()
        strokeWidth = 1f
        style = Paint.Style.STROKE
    }
    /** 暗色网格：主格线（每 5 格，更亮一档，形成结构感） */
    private val paintGridMajor = Paint().apply {
        color = 0x4A9E9E9E.toInt()
        strokeWidth = 1f
        style = Paint.Style.STROKE
    }
    private val paintRemain = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF42A5F5.toInt()
        strokeWidth = 3.5f
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
    }
    private val paintWalked = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF9E9E9E.toInt()
        strokeWidth = 3.5f
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
    }
    private val paintDot = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFFFDD835.toInt() }
    private val paintStart = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF43A047.toInt() }
    private val paintEnd = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFFE53935.toInt() }

    private val ptA = PointF()
    private val ptB = PointF()
    private val clipPath = Path()

    /** 更新数据：整条路线 + 当前位置在其中的索引 */
    fun setData(p: List<GeoPoint>, currentIndex: Int) {
        path = p
        curIndex = currentIndex.coerceIn(0, (p.size - 1).coerceAtLeast(0))
        invalidate()
    }

    /** 经纬度 -> 卡片内像素（北在上） */
    private fun toPx(p: GeoPoint): PointF = PointF(
        ((p.lon - lonMin) * kx * scale).toFloat() + ox,
        ((latMax - p.lat) * scale).toFloat() + oy
    )

    /** 在圆角矩形范围内画暗色网格（次格线 10dp / 主格线每 5 格） */
    private fun drawGrid(canvas: Canvas, w: Float, h: Float) {
        val step = 10f * resources.displayMetrics.density
        if (step < 4f) return
        canvas.save()
        clipPath.reset()
        clipPath.addRoundRect(0f, 0f, w, h, CORNER, CORNER, Path.Direction.CW)
        canvas.clipPath(clipPath)
        var i = 0
        var x = 0f
        while (x <= w) {
            canvas.drawLine(x, 0f, x, h, if (i % 5 == 0) paintGridMajor else paintGridMinor)
            x += step
            i++
        }
        i = 0
        var y = 0f
        while (y <= h) {
            canvas.drawLine(0f, y, w, y, if (i % 5 == 0) paintGridMajor else paintGridMinor)
            y += step
            i++
        }
        canvas.restore()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val w = width.toFloat()
        val h = height.toFloat()
        if (w <= 0f || h <= 0f) return

        canvas.drawRoundRect(0f, 0f, w, h, CORNER, CORNER, paintBg)
        drawGrid(canvas, w, h)                      /* 【M1.5】暗色网格（半透明底之上） */
        canvas.drawRoundRect(1f, 1f, w - 1f, h - 1f, CORNER, CORNER, paintBorder)
        if (path.size < 2) return

        /* 等比投影：北在上，x 用 Δlon·cos(lat) */
        var latMin = Double.MAX_VALUE
        latMax = -Double.MAX_VALUE
        lonMin = Double.MAX_VALUE
        var lonMax = -Double.MAX_VALUE
        for (p in path) {
            if (p.lat < latMin) latMin = p.lat
            if (p.lat > latMax) latMax = p.lat
            if (p.lon < lonMin) lonMin = p.lon
            if (p.lon > lonMax) lonMax = p.lon
        }
        kx = cos(Math.toRadians((latMin + latMax) / 2.0))
        val xSpan = (lonMax - lonMin) * kx
        val ySpan = latMax - latMin
        val span = maxOf(xSpan, ySpan).coerceAtLeast(1e-9)
        val side = minOf(w, h) - PAD * 2f
        scale = side / span
        ox = (w - side) / 2f
        oy = (h - side) / 2f

        /* 降采样索引（保持首尾） */
        val n = path.size
        val step = ((n - 1) / DRAW_MAX).coerceAtLeast(1)
        val idxs = ArrayList<Int>()
        var i = 0
        while (i < n) {
            idxs.add(i)
            i += step
        }
        if (idxs[idxs.size - 1] != n - 1) idxs.add(n - 1)

        drawSeg(canvas, idxs, paintRemain, curIndex, n - 1)   /* 未走（蓝） */
        drawSeg(canvas, idxs, paintWalked, 0, curIndex)       /* 已走（灰） */

        val s = toPx(path[0])
        val t = toPx(path[n - 1])
        val c = toPx(path[curIndex])
        canvas.drawCircle(s.x, s.y, 3.5f, paintStart)
        canvas.drawCircle(t.x, t.y, 3.5f, paintEnd)
        canvas.drawCircle(c.x, c.y, 5f, paintDot)
    }

    /** 画索引区间 [from, to] 内的折线（只连 idxs 里采样到的点） */
    private fun drawSeg(canvas: Canvas, idxs: List<Int>, paint: Paint, from: Int, to: Int) {
        if (from >= to) return
        var started = false
        for (k in idxs) {
            if (k < from || k > to) continue
            val p = toPx(path[k])
            if (!started) {
                ptA.set(p.x, p.y)
                started = true
            } else {
                ptB.set(p.x, p.y)
                canvas.drawLine(ptA.x, ptA.y, ptB.x, ptB.y, paint)
                ptA.set(p.x, p.y)
            }
        }
    }
}
