package com.espnav.app

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.widget.Toast
import android.view.View
import android.widget.SeekBar
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import android.Manifest
import android.content.pm.PackageManager
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import com.espnav.app.data.AmapNavSource
import com.espnav.app.data.MockNavSource
import com.espnav.app.data.NavSource
import com.espnav.app.data.NavStateMapper
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
    private var navSource: NavSource = MockNavSource()
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

        binding.etHost.setText(prefs.getString(KEY_LAST_IP, null) ?: DEFAULT_HOST)
        binding.etPort.setText("8899")
        binding.etFrom.setText(FROM_ADDRESS)
        binding.etTo.setText(TO_ADDRESS)
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
            val f = NavStateMapper.toFrame(navSource.latest())
            send(OutMsg.navFrame(f))
            binding.tvStage.text = "单帧：${navSource.displayName} 剩余 ${f.turnDist} m"
        }
        binding.btnMockStart.setOnClickListener { startMock() }
        binding.btnMockStop.setOnClickListener { stopMock() }
        binding.btnAmapNav.setOnClickListener { startAmapNav() }
        binding.btnCopyLog.setOnClickListener { copyLog() }

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
        if (message.contains("连接失败")) {
            val ip = localIpv4()
            log("本机 IP：$ip")
            if (!ip.startsWith("192.168.4.")) {
                log("⚠ 你当前不在 ESPNav-AP 网段(192.168.4.x)：手机 WLAN 未连上 ESPNav-AP，")
                log("  或被系统自动切回移动数据。请：①关闭 WLAN+/智能网络切换 ②重连 ESPNav-AP")
                log("  更稳的做法：配网让 ESP32 连你手机热点（手机保持开热点+流量）")
            }
        }
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

    /** 开始模拟导航（数据源 = MockNavSource） */
    private fun startMock() {
        launchNav(MockNavSource(), "模拟导航")
    }

    /** 开始高德导航：先申请定位权限，再启动 AMapNavi（目的地 = 当前位置北向 2km，用于验证链路） */
    private fun startAmapNav() {
        if (!client.isConnected) {
            log("未连接，无法开始导航")
            return
        }
        if (!hasInternet()) {
            log("⚠ 手机当前【没有外网】：你连的是 ESPNav-AP（ESP32 热点，无外网），")
            log("  高德导航必须联网才能算路/校验 Key —— 请先配网：让 ESP32 连你手机的热点")
            log("  步骤：手机开热点(2.4GHz) → 手机连 ESPNav-AP → 浏览器 http://192.168.4.1 填热点并保存")
            log("  配好后手机切回自己的热点，App 用「一键连接」连 ESP32 的 IP，再点本按钮")
            return
        }
        if (!hasLocationPermission()) {
            log("需要定位权限，请在弹窗中允许")
            ActivityCompat.requestPermissions(
                this,
                arrayOf(
                    Manifest.permission.ACCESS_FINE_LOCATION,
                    Manifest.permission.ACCESS_COARSE_LOCATION
                ),
                REQ_LOCATION
            )
            return
        }
        val from = binding.etFrom.text.toString().trim().ifBlank { FROM_ADDRESS }
        val to = binding.etTo.text.toString().trim().ifBlank { TO_ADDRESS }
        val src = AmapNavSource(applicationContext, from, to, "北京", emulate = true)
        // 把高德内部日志转发到界面日志区（便于真机诊断）
        src.logSink = { msg -> runOnUiThread { log("高德: " + msg) } }
        launchNav(src, "高德骑行导航")
    }

    /** 取本机 IPv4（用于判断是否处于 ESP32 热点网段 192.168.4.x） */
    private fun localIpv4(): String = runCatching {
        val all = java.net.NetworkInterface.getNetworkInterfaces() ?: return@runCatching "未知"
        for (nif in all) {
            if (!nif.isUp || nif.isLoopback) continue
            for (addr in nif.inetAddresses) {
                if (!addr.isLoopbackAddress && addr is java.net.Inet4Address) {
                    val a = addr.hostAddress
                    if (!a.isNullOrEmpty()) return@runCatching a
                }
            }
        }
        "未知"
    }.getOrDefault("未知")

    /** 是否有可用的外网（高德 SDK 需要联网算路与校验 Key） */
    private fun hasInternet(): Boolean = runCatching {
        val cm = getSystemService(CONNECTIVITY_SERVICE) as ConnectivityManager
        val net = cm.activeNetwork ?: return@runCatching false
        val caps = cm.getNetworkCapabilities(net) ?: return@runCatching false
        caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) &&
            caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED)
    }.getOrDefault(false)

    private fun hasLocationPermission(): Boolean =
        ContextCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) ==
            PackageManager.PERMISSION_GRANTED

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQ_LOCATION) {
            if (grantResults.isNotEmpty() && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                log("定位权限已授予，启动高德导航")
                startAmapNav()
            } else {
                log("定位权限被拒绝，无法使用高德导航")
            }
        }
    }

    /** 统一启动：切换数据源并按 5Hz 向屏幕发帧 */
    private fun launchNav(source: NavSource, label: String) {
        if (mockJob?.isActive == true) return
        if (!client.isConnected) {
            log("未连接，无法开始导航")
            return
        }
        navSource = source
        navSource.start()
        mockJob = lifecycleScope.launch {
            while (isActive) {
                val f = NavStateMapper.toFrame(navSource.latest())
                client.queue(OutMsg.navFrame(f))
                binding.tvStage.text =
                    "${label}：剩余 ${f.turnDist} m  进度 ${f.progressPct}%  ${f.hint}"
                delay(FRAME_INTERVAL_MS)
            }
        }
        log("开始 $label（每 ${FRAME_INTERVAL_MS}ms 一帧）")
    }

    private fun stopMock() {
        mockJob?.cancel()
        mockJob = null
        runCatching { navSource.stop() }
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

    /** 把全部日志复制到剪贴板（便于直接粘贴反馈） */
    private fun copyLog() {
        runCatching {
            val cm = getSystemService(CLIPBOARD_SERVICE) as ClipboardManager
            cm.setPrimaryClip(ClipData.newPlainText("espnav-log", binding.tvLog.text.toString()))
            Toast.makeText(this, "日志已复制到剪贴板", Toast.LENGTH_SHORT).show()
            log("日志已复制到剪贴板（可直接粘贴反馈）")
        }.onFailure { log("复制日志失败：" + it.message) }
    }

    private fun setStatus(text: String) {
        binding.tvStatus.text = text
    }

    private fun log(msg: String) {
        binding.tvLog.append(timeFmt.format(Date()) + "  " + msg + "\n")
        if (binding.tvLog.lineCount > MAX_LOG_LINES) {
            val all = binding.tvLog.text.toString()
            binding.tvLog.text = all.substring(all.length / 3)   // 超限时丢弃最早 1/3
        }
        binding.svLog.post { binding.svLog.fullScroll(View.FOCUS_DOWN) }
    }

    companion object {
        private const val DEFAULT_HOST = "192.168.4.1"
        private const val REQ_LOCATION = 1001
        /** 默认测试起终点（骑行；emulate=true 为模拟行进，室内也可测） */
        private const val FROM_ADDRESS = "北京亦庄泰河三街1号"
        private const val TO_ADDRESS = "北京亦庄同济南路地铁站"
        private const val KEY_LAST_IP = "last_ip"
        private const val FRAME_INTERVAL_MS = 200L   // 5 Hz（协议上限 10fps）
        private const val MAX_LOG_LINES = 1000
    }
}
