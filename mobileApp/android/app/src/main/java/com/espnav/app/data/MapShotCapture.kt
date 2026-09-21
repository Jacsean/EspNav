package com.espnav.app.data

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.ColorMatrix
import android.graphics.ColorMatrixColorFilter
import android.graphics.Paint
import android.graphics.Rect
import android.util.Base64
import com.espnav.app.protocol.OutMsg
import java.io.ByteArrayOutputStream

/**
 * 【M1.5】地图截图 → ESP 底图。
 *
 * 流程：AMap.getMapScreenShot() 取当前导航地图 → 中心裁剪成 4:3 → 缩放到 320×240
 *       → 压暗（亮度 + 饱和度 ColorMatrix，"暗地图"观感）→ JPEG(q70) → base64 分块下发。
 *
 * 与固件 `render/map_image.c` 的约定（改一侧必须同步另一侧）：
 *   · 尺寸固定 320×240（全屏）；上半模式传 320×160（后续需要时再加）；
 *   · 单块原始字节 ≤ 1200 → base64 ≤ 1600 字符（协议单帧 ≤2048B，固件块缓冲 1720）；
 *   · 分块顺序：IMG_BEGIN → N×IMG_CHUNK → IMG_END（同一 seq）。
 */
object MapShotCapture {

    const val OUT_W = 320
    const val OUT_H = 240

    private const val CHUNK_RAW = 1200       /* 每块原始字节数 */
    private const val JPEG_QUALITY = 70      /* 320×240 地图截图约 12–25KB */

    /** 压暗参数（与预览演示的"暗地图"档一致：亮度 45%、饱和度 45%） */
    @Volatile var brightness = 0.45f
    @Volatile var saturation = 0.45f

    /**
     * 把任意尺寸的截图处理成 320×240 JPEG 字节；失败返回 null。
     * 注意：调用方负责回收 src（本函数不回收）。
     */
    fun process(src: Bitmap): ByteArray? = runCatching {
        /* ① 中心裁剪成 4:3 —— 直接缩放会把地图拉变形 */
        val targetRatio = OUT_W.toFloat() / OUT_H.toFloat()
        var cw = src.width
        var ch = (src.width / targetRatio).toInt()
        if (ch > src.height) {
            ch = src.height
            cw = (src.height * targetRatio).toInt()
        }
        val cropped = if (cw == src.width && ch == src.height) src
        else Bitmap.createBitmap(src, (src.width - cw) / 2, (src.height - ch) / 2, cw, ch)

        /* ② 缩放到 320×240 并压暗（ColorMatrix 一步完成，避免逐像素） */
        val out = Bitmap.createBitmap(OUT_W, OUT_H, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(out)
        canvas.drawColor(Color.BLACK)

        val sat = ColorMatrix()
        sat.setSaturation(saturation)
        val lum = brightness
        val dim = ColorMatrix(
            floatArrayOf(
                lum, 0f, 0f, 0f, 0f,
                0f, lum, 0f, 0f, 0f,
                0f, 0f, lum, 0f, 0f,
                0f, 0f, 0f, 1f, 0f
            )
        )
        sat.postConcat(dim)

        val paint = Paint(Paint.FILTER_BITMAP_FLAG)
        paint.colorFilter = ColorMatrixColorFilter(sat)
        canvas.drawBitmap(cropped, null, Rect(0, 0, OUT_W, OUT_H), paint)
        if (cropped !== src) cropped.recycle()

        /* ③ JPEG 编码 */
        val baos = ByteArrayOutputStream()
        out.compress(Bitmap.CompressFormat.JPEG, JPEG_QUALITY, baos)
        out.recycle()
        baos.toByteArray()
    }.getOrNull()

    /**
     * 分块下发：每块一行 JSON，通过 emit 回调顺序送出（emit 由调用方接到 TCP 队列）。
     * 返回发送的块数（0 表示字节为空）。
     */
    fun send(jpg: ByteArray, seq: Int, emit: (String) -> Unit): Int {
        emit(OutMsg.imgBegin(seq, OUT_W, OUT_H, 0, 0, jpg.size))
        var off = 0
        var chunks = 0
        while (off < jpg.size) {
            val n = minOf(CHUNK_RAW, jpg.size - off)
            val b64 = Base64.encodeToString(jpg, off, n, Base64.NO_WRAP)
            emit(OutMsg.imgChunk(seq, b64))
            off += n
            chunks++
        }
        emit(OutMsg.imgEnd(seq))
        return chunks
    }
}
