package com.espnav.app.net

import android.util.Log
import com.espnav.app.protocol.OutMsg
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlin.coroutines.coroutineContext
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.PrintWriter
import java.net.InetSocketAddress
import java.net.Socket

/**
 * TCP 客户端（协议 V1.10）：单主机、行帧（JSON + LF）、PING 心跳、断线回调。
 * 发送统一走内部 Channel，保证顺序且不阻塞主线程（Android 禁止主线程网络 IO）。
 */
class EspNavClient(private val scope: CoroutineScope) {

    interface Listener {
        fun onConnected(addr: String)
        fun onDisconnected(reason: String)
        fun onLine(line: String)
        fun onError(message: String)
    }

    var listener: Listener? = null
    var heartbeatSeconds: Int = 3

    /** 统一把回调切到主线程（UI 更新只能主线程；connect/disconnect 可能从 IO 协程触发） */
    private fun notifyMain(block: () -> Unit) {
        scope.launch(Dispatchers.Main) { block() }
    }

    private var socket: Socket? = null
    private var writer: PrintWriter? = null
    private var readJob: Job? = null
    private var beatJob: Job? = null
    private var sendJob: Job? = null
    private val sendQueue = Channel<String>(Channel.UNLIMITED)

    @Volatile
    private var connectedFlag = false

    val isConnected: Boolean get() = connectedFlag

    fun connect(host: String, port: Int) {
        disconnect("重新连接")
        scope.launch(Dispatchers.IO) {
            try {
                val s = Socket()
                s.tcpNoDelay = true
                s.connect(InetSocketAddress(host, port), CONNECT_TIMEOUT_MS)
                socket = s
                writer = PrintWriter(s.getOutputStream(), false)
                connectedFlag = true
                withContext(Dispatchers.Main) { listener?.onConnected("$host:$port") }

                sendJob = launch { sendLoop() }
                readJob = launch { readLoop(s) }
                beatJob = launch { heartbeat() }

                // 连上先读一次配置，界面可显示设备状态
                queue(OutMsg.getConfig())
            } catch (e: Exception) {
                connectedFlag = false
                withContext(Dispatchers.Main) { listener?.onError("连接失败：${e.message}") }
            }
        }
    }

    /** 顺序发送（线程安全，非阻塞） */
    fun queue(json: String) {
        if (!connectedFlag) {
            notifyMain { listener?.onError("未连接，丢弃报文") }
            return
        }
        sendQueue.trySend(json)
    }

    private suspend fun sendLoop() {
        for (line in sendQueue) {
            val w = writer ?: continue
            try {
                w.print(line)
                w.print('\n')
                w.flush()
            } catch (e: Exception) {
                Log.w(TAG, "send failed: ${e.message}")
            }
        }
    }

    private suspend fun readLoop(s: Socket) {
        var reason = "对端关闭"
        try {
            val reader = BufferedReader(InputStreamReader(s.getInputStream(), Charsets.UTF_8))
            while (coroutineContext.isActive) {
                val line = reader.readLine() ?: break
                if (line.isBlank()) continue
                withContext(Dispatchers.Main) { listener?.onLine(line) }
            }
        } catch (e: Exception) {
            reason = "读取异常：${e.message}"
        } finally {
            connectedFlag = false
            withContext(Dispatchers.Main) { listener?.onDisconnected(reason) }
        }
    }

    private suspend fun heartbeat() {
        while (coroutineContext.isActive) {
            delay(heartbeatSeconds * 1000L)
            val w = writer ?: continue
            try {
                w.print(OutMsg.ping(System.currentTimeMillis() / 1000))
                w.print('\n')
                w.flush()
            } catch (e: Exception) {
                Log.w(TAG, "heartbeat failed: ${e.message}")
            }
        }
    }

    fun disconnect(reason: String) {
        val wasConnected = connectedFlag
        connectedFlag = false
        readJob?.cancel(); readJob = null
        beatJob?.cancel(); beatJob = null
        sendJob?.cancel(); sendJob = null
        try { writer?.close() } catch (_: Exception) {}
        try { socket?.close() } catch (_: Exception) {}
        writer = null
        socket = null
        if (wasConnected) notifyMain { listener?.onDisconnected(reason) }
    }

    companion object {
        private const val TAG = "EspNavClient"
        private const val CONNECT_TIMEOUT_MS = 4000
    }
}
