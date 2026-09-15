package com.espnav.app

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.View
import android.widget.SeekBar
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.espnav.app.data.MockNavigator
import com.espnav.app.databinding.ActivityMainBinding
import com.espnav.app.net.EspNavClient
import com.espnav.app.protocol.InMsg
import com.espnav.app.protocol.OutMsg
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * P1 控制界面：连接 ESP32（TCP :8899）→ 发模拟导航帧 / 调配置 / 看回包日志。
 * 手机需先连接热点 ESPNav-AP（见 strings.xml 的 tip_hotspot）。
 */
class MainActivity : AppCompatActivity(), EspNavClient.Listener {

    private lateinit var binding: ActivityMainBinding
    private lateinit var client: EspNavClient
    private lateinit var mock: MockNavigator
    private var mockJob: Job? = null
    private val prefs by lazy { getSharedPreferences("espnav", MODE_PRIVATE) }
    private val pendingCandidates = ArrayDeque<String>()
    private val timeFmt = SimpleDateFormat("HH:mm:ss", Locale.getDefault())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        client = EspNavClient(lifecycleScope)
        client.listener = this
        mock = MockNavigator()

        binding.etHost.setText(prefs.getString(KEY_LAST_IP, null) ?: DEFAULT_HOST)
        binding.etPort.setText("8899")
        binding.btnDisconnect.isEnabled = false

        binding.btnConnect.setOnClickListener { doConnect() }
        binding.btnQuickConnect.setOnClickListener { quickConnect() }
        binding.btnOpenProv.setOnClickListener { openProvPage() }
        binding.btnDisconnect.setOnClickListener {
            stopMock()
            client.disconnect("手动断开")
        }
        binding.btnPing.setOnClickListener { send(OutMsg.ping(System.currentTimeMillis() / 1000)) }
        binding.btnGetConfig.setOnClickListener { send(OutMsg.getConfig()) }
        binding.btnClear.setOnClickListener { send(OutMsg.clearScreen()) }
        binding.btnSendOnce.setOnClickListener {
            val f = mock.next()
            send(OutMsg.navFrame(f))
            binding.tvStage.text = "单帧：${mock.stageName()} 剩余 ${f.turnDist} m"
        }
        binding.btnMockStart.setOnClickListener { startMock() }
        binding.btnMockStop.setOnClickListener { stopMock() }

        binding.seekBrightness.progress = 80
        binding.seekDashSpeed.progress = 40
        binding.switchAnim.isChecked = true

        binding.seekBrightness.setOnSeekBarChangeListener(
            onSeek("亮度") { v -> send(OutMsg.setConfig(brightness = v)) }
        )
        binding.seekDashSpeed.setOnSeekBarChangeListener(
            onSeek("速度") { v -> send(OutMsg.setConfig(dashSpeed = v)) }
        )
        binding.switchAnim.setOnCheckedChangeListener { _, checked ->
            send(OutMsg.setConfig(animEnable = checked))
            log("设置 流动动画 = $checked")
        }

        log("就绪：请先连接热点 ESPNav-AP，再点“连接”")
    }

    override fun onDestroy() {
        stopMock()
        client.disconnect("页面关闭")
        super.onDestroy()
    }

    // ---------------- 网络回调（都在主线程） ----------------

    override fun onConnected(addr: String) {
        prefs.edit().putString(KEY_LAST_IP, addr.substringBefore(':')).apply()   /* 记住可用地址 */
        setStatus("已连接 $addr")
        binding.btnConnect.isEnabled = false
        binding.btnDisconnect.isEnabled = true
        log("已连接 $addr")
    }

    override fun onDisconnected(reason: String) {
        stopMock()
        setStatus("未连接（$reason）")
        binding.btnConnect.isEnabled = true
        binding.btnDisconnect.isEnabled = false
        log("断开：$reason")
    }

    override fun onLine(line: String) {
        log("← " + InMsg.describe(line))
    }

    override fun onError(message: String) {
        log("! " + message)
    }

    // ---------------- 操作 ----------------

    /** 一键连接：依次尝试“上次成功的地址”和 192.168.4.1；4.5 秒未连上就换下一个 */
    private fun quickConnect() {
        val list = LinkedHashSet<String>()
        prefs.getString(KEY_LAST_IP, null)?.takeIf { it.isNotBlank() }?.let { list.add(it) }
        list.add(binding.etHost.text.toString().trim().ifBlank { DEFAULT_HOST })
        list.add(DEFAULT_HOST)
        pendingCandidates.clear()
        pendingCandidates.addAll(list)
        log("一键连接：候选地址 ${list.joinToString(" -> ")}")
        tryNextCandidate()
    }

    private fun tryNextCandidate() {
        val c = pendingCandidates.removeFirstOrNull()
        if (c == null) {
            log("一键连接失败：请确认手机已连上热点 ESPNav-AP（密码 espnav1234）")
            log("或在浏览器打开 http://192.168.4.1 完成配网后重试")
            return
        }
        binding.etHost.setText(c)
        doConnect()
        lifecycleScope.launch {
            delay(4500)
            if (!client.isConnected) tryNextCandidate()
        }
    }

    private fun openProvPage() {
        try {
            startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("http://" + DEFAULT_HOST)))
        } catch (e: Exception) {
            log("打不开浏览器：${e.message}")
        }
    }

    private fun doConnect() {
        val host = binding.etHost.text.toString().trim().ifBlank { "192.168.4.1" }
        val port = binding.etPort.text.toString().trim().toIntOrNull() ?: 8899
        log("连接 $host:$port ...")
        client.connect(host, port)
    }

    private fun startMock() {
        if (mockJob?.isActive == true) return
        if (!client.isConnected) {
            log("未连接，无法开始模拟")
            return
        }
        mock.reset()
        mockJob = lifecycleScope.launch {
            while (isActive) {
                val f = mock.next()
                client.queue(OutMsg.navFrame(f))
                binding.tvStage.text =
                    "模拟中：${mock.stageName()}  剩余 ${f.turnDist} m  进度 ${f.progressPct}%"
                delay(FRAME_INTERVAL_MS)
            }
        }
        log("开始模拟导航（每 ${FRAME_INTERVAL_MS}ms 一帧）")
    }

    private fun stopMock() {
        mockJob?.cancel()
        mockJob = null
    }

    private fun send(json: String) {
        if (client.isConnected) client.queue(json) else log("未连接，无法发送")
    }

    private fun onSeek(tag: String, action: (Int) -> Unit) = object : SeekBar.OnSeekBarChangeListener {
        override fun onProgressChanged(sb: SeekBar?, progress: Int, fromUser: Boolean) {
            if (fromUser) binding.tvStage.text = "调整 $tag：$progress"
        }
        override fun onStartTrackingTouch(sb: SeekBar?) = Unit
        override fun onStopTrackingTouch(sb: SeekBar?) {
            val v = sb?.progress ?: return
            action(v)
            log("设置 $tag = $v")
        }
    }

    private fun setStatus(text: String) {
        binding.tvStatus.text = text
    }

    private fun log(msg: String) {
        binding.tvLog.append(timeFmt.format(Date()) + "  " + msg + "\n")
        if (binding.tvLog.lineCount > MAX_LOG_LINES) {
            val all = binding.tvLog.text.toString()
            binding.tvLog.text = all.substring(all.length / 3)
        }
        binding.svLog.post { binding.svLog.fullScroll(View.FOCUS_DOWN) }
    }

    companion object {
        private const val DEFAULT_HOST = "192.168.4.1"
        private const val KEY_LAST_IP = "last_ip"
        private const val FRAME_INTERVAL_MS = 200L   // 5 Hz（协议上限 10fps）
        private const val MAX_LOG_LINES = 200
    }
}
