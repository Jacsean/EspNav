package com.espnav.app.ui

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.PointF
import android.util.AttributeSet
import android.view.View
import com.espnav.app.data.GeoPoint
import kotlin.math.cos

/**
 * 导航态右下角的「行程图」卡片（与 ESP 屏右上角行程图对应）。
 *
 * 画法：整条路线缩略 —— 北在上、等比缩放到正方形内，x 方向按 cos(纬度) 折算
 * （与 App 的 OverviewProjector、固件 draw_overview 同一套做法，避免东西向被拉伸）。
 * 已走段灰色、未走段蓝色、起点绿、终点红、当前位置黄点。
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

    private val paintBg = Paint().apply { color = 0xE00A0A0A.toInt() }
    private val paintBorder = Paint().apply {
        color = 0x662E7D32
        strokeWidth = 1.5f
        style = Paint.Style.STROKE
    }
    private val paintCross = Paint().apply { color = 0x222E7D32; strokeWidth = 1f }
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

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val w = width.toFloat()
        val h = height.toFloat()
        if (w <= 0f || h <= 0f) return

        canvas.drawRoundRect(0f, 0f, w, h, 10f, 10f, paintBg)
        canvas.drawRoundRect(1f, 1f, w - 1f, h - 1f, 10f, 10f, paintBorder)
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

        /* 十字参考线（弱） */
        canvas.drawLine(ox + side / 2f, oy, ox + side / 2f, oy + side, paintCross)
        canvas.drawLine(ox, oy + side / 2f, ox + side, oy + side / 2f, paintCross)

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
