package com.espnav.app.data

import android.content.Context
import android.speech.tts.TextToSpeech
import android.speech.tts.UtteranceProgressListener
import android.util.Base64
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.Locale

/**
 * 【M21】语音播报：Android 的 TextToSpeech 合成 → 解析 WAV → 转 **16kHz/16bit/单声道** PCM
 * → base64 分块 → 通过 [send] 回调交给 MainActivity 下发（AUDIO_BEGIN/CHUNK/END）。
 *
 * 为什么走这条路（方案 A）：ESP32 侧只负责"放 PCM"（ES8311 + I2S），不跑 TTS、不解码压缩音频。
 * 采样率/位深/声道必须与固件一致（audio_rx 只接受 16000/16/1）。
 *
 * 用法：
 *   val t = TtsSpeaker(ctx)
 *   t.init { json -> client.queue(json) }      // 注入"发送一帧 JSON"的方式
 *   t.speak("前方一百米右转")
 *   t.release()
 */
class TtsSpeaker(private val ctx: Context) {

    companion object {
        const val TARGET_RATE = 16000      /* 固件固定要求 */
        const val CHUNK_RAW = 1200         /* 每块原始字节（base64 后 1600 < 固件 1800 缓冲） */
    }

    private var tts: TextToSpeech? = null
    private var ready = false
    private var send: ((String) -> Unit)? = null
    private var seq = 0
    private var pending: String? = null     /* TTS 尚未就绪时的待播文本 */

    /** 【M23】是否正在合成/播放：让后续播报"不插队"（一帧一句，不叠加） */
    @Volatile
    var isSpeaking = false
        private set

    fun init(onSend: (String) -> Unit) {
        send = onSend
        tts = TextToSpeech(ctx.applicationContext) { status ->
            ready = status == TextToSpeech.SUCCESS
            if (!ready) return@TextToSpeech
            runCatching { tts?.language = Locale.CHINA }        /* 中文（失败则用引擎默认） */
            runCatching {
                tts?.setOnUtteranceProgressListener(object : UtteranceProgressListener() {
                    override fun onStart(utteranceId: String?) = Unit
                    override fun onError(utteranceId: String?) = Unit
                    override fun onDone(utteranceId: String?) {
                        utteranceId?.let { onSynthDone(it) }
                    }
                })
            }
            pending?.let { p -> pending = null; speak(p) }       /* 就绪后补播 */
        }
    }

    /** 播报一段文本（合成失败/未就绪则静默忽略，绝不影响导航） */
    fun speak(text: String) {
        val t = tts
        if (t == null || !ready) { pending = text; return }
        runCatching {
            val f = File(ctx.cacheDir, "espnav_tts.wav")
            if (f.exists()) f.delete()
            /* 以 utteranceId 携带文本编号，onDone 时用来读文件 */
            val id = "espnav" + (++seq)
            isSpeaking = true
            t.synthesizeToFile(text, null, f, id)
        }
    }

    fun release() {
        runCatching { tts?.stop() }
        runCatching { tts?.shutdown() }
        tts = null
        ready = false
    }

    // ---------------- 合成完成 → 处理并下发 ----------------
    /** ⚠ 本回调在 **TTS 引擎线程**，不是主线程；且读文件 + 重采样有一定耗时，
     * 所以整段处理再丢到后台线程，避免阻塞任何 UI。发送动作由 MainActivity 切回主线程。 */
    private fun onSynthDone(utteranceId: String) {
        val out = send ?: return
        Thread {
            runCatching {
                val f = File(ctx.cacheDir, "espnav_tts.wav")
                if (!f.isFile) return@runCatching
                val wav = f.readBytes()
                val pcm = wavToPcm16kMono(wav) ?: return@runCatching
                if (pcm.isEmpty()) return@runCatching

                val mySeq = seq
                out(com.espnav.app.protocol.OutMsg.audioBegin(
                    mySeq, TARGET_RATE, 16, 1, pcm.size
                ))
                var off = 0
                while (off < pcm.size) {
                    val n = minOf(CHUNK_RAW, pcm.size - off)
                    val b64 = Base64.encodeToString(pcm, off, n, Base64.NO_WRAP)   /* NO_WRAP：不能有换行 */
                    out(com.espnav.app.protocol.OutMsg.audioChunk(mySeq, b64))
                    off += n
                }
                out(com.espnav.app.protocol.OutMsg.audioEnd(mySeq))
                f.delete()
            }
            /* 【M23】复位必须放在 runCatching **之外** —— 否则中途异常会让"正在播"永久卡住，
             * 之后所有播报都被 announce() 静默吞掉（Review 时发现的坑）。 */
            isSpeaking = false
        }.start()
    }

    // ---------------- WAV → 16kHz/16bit/单声道 ----------------
    /**
     * 极简 WAV 解析：按 "fmt " / "data" 块遍历（比固定偏移稳妥），取出采样率/声道/位深与 PCM。
     * 然后：多声道 → 混为单声道；8bit → 16bit；非 16k → 线性插值重采样到 16k。
     */
    private fun wavToPcm16kMono(wav: ByteArray): ByteArray? {
        if (wav.size < 44) return null
        val bb = ByteBuffer.wrap(wav).order(ByteOrder.LITTLE_ENDIAN)
        if (wav[0] != 'R'.code.toByte() || wav[1] != 'I'.code.toByte()) return null

        var ch = 1
        var rate = TARGET_RATE
        var bits = 16
        var dataOff = -1
        var dataLen = 0

        var p = 12
        while (p + 8 <= wav.size) {
            val id = String(wav, p, 4, Charsets.US_ASCII)
            val sz = bb.getInt(p + 4)
            val body = p + 8
            when (id) {
                "fmt " -> {
                    if (body + 16 > wav.size) return null
                    ch = bb.getShort(body + 2).toInt()
                    rate = bb.getInt(body + 4)
                    bits = bb.getShort(body + 14).toInt()
                }
                "data" -> {
                    dataOff = body
                    dataLen = minOf(sz, wav.size - body)
                    break
                }
            }
            if (sz <= 0) break
            p = body + sz + (sz and 1)          /* 块按偶数字节对齐 */
        }
        if (dataOff < 0 || dataLen <= 0 || ch <= 0) return null

        /* 1) 取样本为 Short（先按 16bit 处理；8bit 另算） */
        val bytesPerSample = if (bits == 8) 1 else 2
        val frameBytes = bytesPerSample * ch
        if (frameBytes <= 0) return null
        val frames = dataLen / frameBytes
        if (frames <= 0) return null

        val mono = ShortArray(frames)
        for (i in 0 until frames) {
            var acc = 0
            for (c in 0 until ch) {
                val o = dataOff + i * frameBytes + c * bytesPerSample
                val v: Int = if (bits == 8) {
                    (wav[o].toInt() and 0xFF) - 128 shl 8      /* 无符号 8bit → 16bit */
                } else {
                    (wav[o].toInt() and 0xFF) or (wav[o + 1].toInt() shl 8)   /* 小端 16bit 有符号 */
                }
                acc += v
            }
            mono[i] = (acc / ch).toShort()
        }

        /* 2) 重采样到 TARGET_RATE（线性插值） */
        val src = if (rate == TARGET_RATE) mono else resample(mono, rate, TARGET_RATE)
        if (src.isEmpty()) return null

        /* 3) 打包为小端 16bit PCM */
        val out = ByteArray(src.size * 2)
        val ob = ByteBuffer.wrap(out).order(ByteOrder.LITTLE_ENDIAN)
        for (i in src.indices) ob.putShort(i * 2, src[i])
        return out
    }

    private fun resample(src: ShortArray, from: Int, to: Int): ShortArray {
        if (from <= 0 || to <= 0) return ShortArray(0)
        val n = (src.size.toLong() * to / from).toInt()
        if (n <= 0) return ShortArray(0)
        val out = ShortArray(n)
        val step = from.toDouble() / to.toDouble()
        for (i in 0 until n) {
            val x = i * step
            val i0 = x.toInt()
            val i1 = if (i0 + 1 < src.size) i0 + 1 else i0
            val frac = x - i0
            val v = src[i0] * (1 - frac) + src[i1] * frac
            out[i] = v.toInt().coerceIn(-32768, 32767).toShort()
        }
        return out
    }
}
