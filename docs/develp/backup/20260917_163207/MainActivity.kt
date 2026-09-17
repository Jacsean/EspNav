package com.espnav.app

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.core.widget.doAfterTextChanged
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
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
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
    /** 连接尝试互斥：避免“候选链/自动重连/扫描”三者并发建连（多连接会互相踢，屏幕反复切画面） */
    /** 习惯性配置（连接地址/默认起终点/城市/缩放/日志行数…），由「⚙ 设置」维护 */
    private val appPrefs by lazy { com.espnav.app.data.AppPrefs(applicationContext) }

    private var connecting = false
    /** 主动断开标记：点「断开」/页面关闭时置位，用于跳过自动重连（否则会被立刻拉回连接） */
    private var intentionalDisconnect = false
    /** 途经点（最多 MAX_VIA 个）：地址文本 + 解析到的坐标；顺序即生效顺序 */
    private val viaTexts = mutableListOf<String>()
    private val viaPoints = mutableListOf<com.amap.api.maps.model.LatLng?>()
    private val viaMarkers = mutableListOf<com.amap.api.maps.model.Marker?>()

    /** 算路超时兜底任务（高德回调不返回时不至于一直“正在算路…”） */
    private var routeTimeoutJob: kotlinx.coroutines.Job? = null
    private var previewLenM = 0      /* 预览得到的全程（米） */
    private var previewSecS = 0      /* 预览得到的预计耗时（秒） */
    private val timeFmt = SimpleDateFormat("HH:mm:ss", Locale.getDefault())
    private val crashFile: java.io.File get() = java.io.File(filesDir, "crash.log")
    private var reconnectCount = 0

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        savedBundle = savedInstanceState
        installCrashHandler()
        setupTabs()

        client = EspNavClient(lifecycleScope)
        client.listener = this
        refreshActionStates()          /* 必须放在 client 初始化之后：内部会读 client.isConnected */

        binding.etHost.setText(prefs.getString(KEY_LAST_IP, null) ?: appPrefs.host)
        binding.etPort.setText(appPrefs.port.toString())
        binding.etFrom.setText(appPrefs.fromAddress)
        binding.etTo.setText(appPrefs.toAddress)
        binding.btnDisconnect.isEnabled = false
        if (appPrefs.autoConnectOnStart) {
            log("配置项「启动后自动连接」已开启 → 自动发起一键连接")
            binding.root.postDelayed({ if (!client.isConnected) quickConnect() }, 800)
        }

        for (et in listOf(binding.etFrom, binding.etTo)) {
            et.doAfterTextChanged { updatePickState() }
        }
        binding.btnConnect.setOnClickListener { doConnect() }
        binding.btnQuickConnect.setOnClickListener { quickConnect() }
        binding.btnOpenProv.setOnClickListener { openProvPage() }
        binding.btnSettings.setOnClickListener { openSettings() }
        /* 摘要行「编辑」：展开/收起起终点输入框（默认收起，把屏幕让给地图） */
        binding.tvEditToggle.setOnClickListener {
            val show = binding.routeEditPanel.visibility != View.VISIBLE
            binding.routeEditPanel.visibility = if (show) View.VISIBLE else View.GONE
            binding.tvEditToggle.text = getString(if (show) R.string.btn_edit_collapse else R.string.btn_edit_points)
        }
        binding.btnBackToConnect.setOnClickListener { showTab(false) }
        binding.btnAddVia.setOnClickListener { addVia() }
        /* 日志区是公共组件（两个 Tab 都可见），点标题可折叠/展开 */
        binding.logHeader.setOnClickListener {
            val show = binding.svLog.visibility != View.VISIBLE
            binding.svLog.visibility = if (show) View.VISIBLE else View.GONE
            binding.tvLogTitle.text = getString(R.string.label_log) + (if (show) "  ▾" else "  ▸")
        }
        binding.btnDisconnect.setOnClickListener {
            intentionalDisconnect = true                  /* 手动断开：不自动重连 */
            stopMock()
            send(OutMsg.bye())
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

        if (crashFile.exists() && crashFile.length() > 0) {
            val txt = crashFile.readText()
            log("⚠ 上次运行崩溃日志（供反馈，已另存为 crash.old.log）：")
            log(txt.takeLast(1500))
            runCatching { crashFile.renameTo(java.io.File(filesDir, "crash.old.log")) }
        }
        log("就绪：请先连接热点 ESPNav-AP，再点“连接”")
    }

    override fun onResume() {
        super.onResume()
        /* 只有导航页可见时才驱动 MapView 生命周期（官方要求 onCreate/onResume/onPause/onDestroy 成对） */
        if (binding.pageNav.visibility == View.VISIBLE) ensureMap()
    }

    /** 导航页是全屏的：返回键先切回连接页，再按才退出 */
    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (binding.pageNav.visibility == View.VISIBLE) { showTab(false); return }
        @Suppress("DEPRECATION")
        super.onBackPressed()
    }

    override fun onPause() {
        if (binding.pageNav.visibility == View.VISIBLE) {
            runCatching { binding.mapView.onPause() }
        }
        super.onPause()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        runCatching { binding.mapView.onSaveInstanceState(outState) }
    }

    override fun onDestroy() {
        stopMock()
        intentionalDisconnect = true
        send(OutMsg.bye())
        client.disconnect("页面关闭")
        super.onDestroy()
    }

    // ---------------- 网络回调（都在主线程） ----------------

    /** 断线自动重连（App 切后台/网络抖动后会自己恢复，最多 5 次） */
    private fun scheduleReconnect(reason: String) {
        if (reconnectCount >= MAX_RECONNECT) {
            log("自动重连已达上限（$MAX_RECONNECT 次），请手动点「一键连接」")
            return
        }
        reconnectCount++
        log("连接中断（" + reason + "），" + (RECONNECT_DELAY_MS / 1000) + " 秒后自动重连（第 " + reconnectCount + " 次）")
        lifecycleScope.launch {
            delay(RECONNECT_DELAY_MS)
            if (!client.isConnected && !connecting) { connecting = true; doConnect() }
        }
    }

    override fun onConnected(addr: String) {
        connecting = false
        intentionalDisconnect = false
        prefs.edit().putString(KEY_LAST_IP, addr.substringBefore(':')).apply()   /* 记住可用地址 */
        reconnectCount = 0
        NavService.start(this)                    /* 熄屏保持连接 */
        requestNotifyPermissionIfNeeded()
        setStatus("已连接 $addr")
        refreshActionStates()          /* 连接成功：相关操作解灰（用户要求：启动成功后刷新一次） */
        log("已连接 $addr")
        send(OutMsg.hello())                 /* 握手：告知 ESP32 “App 已上线” */
    }

    override fun onDisconnected(reason: String) {
        stopMock()
        if (intentionalDisconnect) {                     /* 主动断开：不再自动重连 */
            intentionalDisconnect = false
            log("已手动断开，不自动重连（如需恢复请点「一键连接」）")
        } else {
            scheduleReconnect(reason)                    /* 被动断开：保持自动重连 */
        }
        setStatus("未连接（$reason）")
        refreshActionStates()          /* 断开：重新置灰 */
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
        if (client.isConnected) { log("已连接，无需重连"); return }
        if (connecting) { log("连接进行中，忽略重复请求"); return }
        connecting = true
        val list = LinkedHashSet<String>()
        prefs.getString(KEY_LAST_IP, null)?.takeIf { it.isNotBlank() }?.let { list.add(it) }
        list.add(binding.etHost.text.toString().trim().ifBlank { DEFAULT_HOST })
        list.add(DEFAULT_HOST)
        // 当前所在子网的网关（手机热点网关通常就是 ESP32 所在网段的第一跳）
        val my = localIpv4()
        if (my.count { it == '.' } == 3) list.add(my.substringBeforeLast('.') + ".1")
        pendingCandidates.clear()
        pendingCandidates.addAll(list)
        log("一键连接：候选地址 ${list.joinToString(" -> ")}")
        tryNextCandidate()
    }

    private fun tryNextCandidate() {
        if (client.isConnected) { connecting = false; return }
        val c = pendingCandidates.removeFirstOrNull()
        if (c == null) {
            log("已知地址都未连上 → 开始扫描当前局域网（8899）…")
            scanForDevice { ips ->
                if (ips.isEmpty()) {
                    log("扫描未发现设备：请确认手机与 ESP32 在同一网络（ESP32 已配网连上本热点，或手机连了 ESPNav-AP）")
                    connecting = false
                } else {
                    log("扫描发现：${ips.joinToString(", ")} → 依次尝试")
                    pendingCandidates.addAll(ips)
                    tryNextCandidate()
                }
            }
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
        val from = binding.etFrom.text.toString().trim().ifBlank { appPrefs.fromAddress }
        val to = binding.etTo.text.toString().trim().ifBlank { appPrefs.toAddress }
        val src = AmapNavSource(applicationContext, from, to, appPrefs.geoCity, emulate = true)
        // 把高德内部日志转发到界面日志区（便于真机诊断）
        src.logSink = { msg -> runOnUiThread { log("高德: " + msg) } }
        launchNav(src, "高德骑行导航")
    }

    /** 在当前所在子网内扫描 8899 端口，自动发现 ESP32（手机热点/家里路由/ESP 热点都适用） */
    private fun scanForDevice(onDone: (List<String>) -> Unit) {
        val my = localIpv4()
        if (my == "未知" || my.count { it == '.' } != 3) {
            onDone(emptyList())
            return
        }
        val prefix = my.substringBeforeLast('.')
        log("开始扫描子网 $prefix.0/24 的 8899 端口（约 5 秒）…")
        lifecycleScope.launch(Dispatchers.IO) {
            val found = java.util.Collections.synchronizedList(mutableListOf<String>())
            val pool = java.util.concurrent.Executors.newFixedThreadPool(32)
            val latch = java.util.concurrent.CountDownLatch(254)
            for (i in 1..254) {
                pool.execute {
                    try {
                        java.net.Socket().use { sk ->
                            sk.connect(java.net.InetSocketAddress("$prefix.$i", 8899), 220)
                            found.add("$prefix.$i")
                        }
                    } catch (_: Exception) {
                    } finally {
                        latch.countDown()
                    }
                }
            }
            latch.await(7, java.util.concurrent.TimeUnit.SECONDS)
            pool.shutdownNow()
            withContext(Dispatchers.Main) { onDone(found.toList()) }
        }
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

    /** Android 13+ 需要通知权限，前台服务才能显示常驻通知 */
    private fun requestNotifyPermissionIfNeeded() {
        if (android.os.Build.VERSION.SDK_INT < 33) return
        if (checkSelfPermission("android.permission.POST_NOTIFICATIONS") ==
            PackageManager.PERMISSION_GRANTED
        ) return
        ActivityCompat.requestPermissions(
            this, arrayOf("android.permission.POST_NOTIFICATIONS"), REQ_NOTIFY
        )
    }

    private fun hasLocationPermission(): Boolean =
        ContextCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) ==
            PackageManager.PERMISSION_GRANTED

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQ_LOCATION) {                     /* 连接页：高德骑行导航 */
            if (grantResults.isNotEmpty() && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                log("定位权限已授予，启动高德导航")
                startAmapNav()
            } else {
                log("定位权限被拒绝，无法使用高德导航")
            }
        } else if (requestCode == REQ_LOCATION_PREVIEW) {      /* 导航页：只继续预览，绝不启动导航 */
            if (grantResults.isNotEmpty() && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                log("定位权限已授予，继续预览路线（不会自动开始导航）")
                previewRoute()
            } else {
                log("定位权限被拒绝：预览路线需要定位（算路起点/显示当前位置）")
                toast("预览路线需要定位权限，请在系统设置中允许")
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
        runCatching { binding.mapView.onDestroy() }
        NavService.stop(this)
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

    /** 安装全局崩溃捕获：把堆栈写入 App 私有目录，下次启动时显示，便于反馈定位 */
    /** 安装全局崩溃捕获：把堆栈写入 App 私有目录，下次启动时显示，便于反馈定位 */
    private fun installCrashHandler() {
        val def = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { t, e ->
            runCatching {
                val nl = System.lineSeparator()
                crashFile.appendText("=== " + java.util.Date().toString() + "  thread=" + t.name + " ===")
                crashFile.appendText(nl)
                crashFile.appendText(android.util.Log.getStackTraceString(e))
                crashFile.appendText(nl)
            }
            def?.uncaughtException(t, e)      // 仍交给系统（会显示"应用已停止"）
        }
    }

    // ---------------- Tab 与地图（Tab2 导航） ----------------

    private var aMap: com.amap.api.maps.AMap? = null
    private var previewLine: com.amap.api.maps.model.Polyline? = null
    private var previewSource: AmapNavSource? = null
    private var mapReady = false
    private var mapCenteredOnce = false
    private var savedBundle: Bundle? = null
    private var startMarker: com.amap.api.maps.model.Marker? = null
    private var endMarker: com.amap.api.maps.model.Marker? = null
    private var startLatLng: com.amap.api.maps.model.LatLng? = null
    private var endLatLng: com.amap.api.maps.model.LatLng? = null

    /** L2b：导航页全屏（隐藏 Tab 栏，让地图占约 8 成）；返回按钮/返回键切回连接页 */
    private fun applyTabUi(nav: Boolean) {
        binding.tabMain.visibility = if (nav) View.GONE else View.VISIBLE
        binding.pageConnect.visibility = if (nav) View.GONE else View.VISIBLE
        binding.pageNav.visibility = if (nav) View.VISIBLE else View.GONE
        if (nav) ensureMap() else runCatching { binding.mapView.onPause() }
        refreshActionStates()
    }

    private fun showTab(nav: Boolean) {
        binding.tabMain.getTabAt(if (nav) 1 else 0)?.select()   /* 触发 onTabSelected -> applyTabUi */
        applyTabUi(nav)                                          /* 保险：Tab 被隐藏时也确保切页生效 */
    }

    /** 「⚙ 设置」：修改习惯性配置（持久化）；权限只能显示状态并跳系统设置（Android 不允许 App 自改） */
    private fun openSettings() {
        val v = layoutInflater.inflate(R.layout.dialog_settings, null)
        fun ed(id: Int) = v.findViewById<android.widget.EditText>(id)
        val cbAutoConn = v.findViewById<android.widget.CheckBox>(R.id.setAutoConnect)
        val cbAutoCity = v.findViewById<android.widget.CheckBox>(R.id.setAutoCity)

        ed(R.id.setHost).setText(appPrefs.host)
        ed(R.id.setPort).setText(appPrefs.port.toString())
        cbAutoConn.isChecked = appPrefs.autoConnectOnStart
        ed(R.id.setFrom).setText(appPrefs.fromAddress)
        ed(R.id.setTo).setText(appPrefs.toAddress)
        cbAutoCity.isChecked = appPrefs.autoCity
        ed(R.id.setCity).setText(appPrefs.geoCity)
        ed(R.id.setZoom).setText(appPrefs.defaultZoom.toString())
        ed(R.id.setLogLines).setText(appPrefs.maxLogLines.toString())
        v.findViewById<android.widget.TextView>(R.id.tvPermState).text =
            if (hasLocationPermission()) getString(R.string.set_perm_granted)
            else getString(R.string.set_perm_denied)

        v.findViewById<android.widget.Button>(R.id.btnOpenAppSettings).setOnClickListener {
            runCatching {
                startActivity(
                    android.content.Intent(
                        android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                        android.net.Uri.parse("package:" + packageName)
                    )
                )
            }.onFailure { log("打不开系统设置：" + it.message) }
        }

        androidx.appcompat.app.AlertDialog.Builder(this)
            .setTitle(R.string.btn_settings)
            .setView(v)
            .setPositiveButton("保存") { _, _ ->
                appPrefs.host = ed(R.id.setHost).text.toString().trim().ifBlank { com.espnav.app.data.AppPrefs.DEF_HOST }
                appPrefs.port = ed(R.id.setPort).text.toString().trim().toIntOrNull() ?: com.espnav.app.data.AppPrefs.DEF_PORT
                appPrefs.autoConnectOnStart = cbAutoConn.isChecked
                appPrefs.fromAddress = ed(R.id.setFrom).text.toString().trim().ifBlank { com.espnav.app.data.AppPrefs.DEF_FROM }
                appPrefs.toAddress = ed(R.id.setTo).text.toString().trim().ifBlank { com.espnav.app.data.AppPrefs.DEF_TO }
                appPrefs.autoCity = cbAutoCity.isChecked
                appPrefs.geoCity = ed(R.id.setCity).text.toString().trim().ifBlank { "北京" }
                appPrefs.defaultZoom = (ed(R.id.setZoom).text.toString().trim().toFloatOrNull() ?: 16f).coerceIn(3f, 19f)
                appPrefs.maxLogLines = (ed(R.id.setLogLines).text.toString().trim().toIntOrNull() ?: 1000).coerceIn(100, 20000)
                applyPrefsToUi()
                log(getString(R.string.set_saved))
            }
            .setNegativeButton("取消", null)
            .show()
    }

    /** 把配置回填到界面控件 */
    private fun applyPrefsToUi() {
        binding.etHost.setText(appPrefs.host)
        binding.etPort.setText(appPrefs.port.toString())
        binding.etFrom.setText(appPrefs.fromAddress)
        binding.etTo.setText(appPrefs.toAddress)
        updatePickState()
    }

    private fun setupTabs() {
        val tab = binding.tabMain
        tab.addTab(tab.newTab().setText(R.string.tab_connect))
        tab.addTab(tab.newTab().setText(R.string.tab_nav))
        tab.addOnTabSelectedListener(object :
            com.google.android.material.tabs.TabLayout.OnTabSelectedListener {
            override fun onTabSelected(t: com.google.android.material.tabs.TabLayout.Tab) {
                applyTabUi(t.position == 1)
            }

            override fun onTabUnselected(t: com.google.android.material.tabs.TabLayout.Tab) = Unit
            override fun onTabReselected(t: com.google.android.material.tabs.TabLayout.Tab) = Unit
        })

        binding.btnUseMapNav.setOnClickListener { previewRoute() }
        binding.btnStartNav.setOnClickListener { startConfirmedNav() }
        binding.btnGiveUp.setOnClickListener { giveUpPreview() }
        binding.btnMapClear.setOnClickListener {
            startMarker?.remove()
            endMarker?.remove()
            startMarker = null
            endMarker = null
            startLatLng = null
            endLatLng = null
            updatePickState()
            log("已清除地图标记")
        }
    }

    /** 刷新“起点/终点是否已选”的显眼状态行 */
    /** 按连接状态刷新所有操作可用性：未连接时除“连接/一键连接/配网页/复制日志”外一律置灰 */
    private fun refreshActionStates() {
        if (!::client.isInitialized) return        /* 防御：client 未初始化时不动作（避免启动即崩） */
        val on = client.isConnected
        binding.btnConnect.isEnabled = !on
        binding.btnQuickConnect.isEnabled = !on
        binding.btnDisconnect.isEnabled = on
        for (v in listOf<android.view.View>(
                binding.btnSendOnce, binding.btnClear, binding.btnMockStart, binding.btnMockStop,
                binding.btnAmapNav, binding.btnPing, binding.btnGetConfig,
                binding.seekBrightness, binding.seekDashSpeed, binding.switchAnim)) {
            v.isEnabled = on
        }
        updatePickState()                     /* 导航页的操作也随连接状态刷新 */
        log("操作可用性已刷新：" + (if (on) "已连接 · 操作可用" else "未连接 · 仅连接/配网可用"))
    }

    // ---------------- 途经点（v1+v2：增/删/✕；长按排序菜单）----------------

    private fun rebuildViaRows() {
        binding.viaContainer.removeAllViews()
        val ctx = this
        for (i in viaTexts.indices) {
            val row = android.widget.LinearLayout(ctx).apply {
                orientation = android.widget.LinearLayout.HORIZONTAL
            }
            val tag = android.widget.TextView(ctx).apply {
                text = getString(R.string.label_via) + (i + 1)
                setPadding(0, 0, 8, 0)
            }
            val et = android.widget.EditText(ctx).apply {
                layoutParams = android.widget.LinearLayout.LayoutParams(0, android.view.ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
                inputType = android.text.InputType.TYPE_CLASS_TEXT
                importantForAutofill = android.view.View.IMPORTANT_FOR_AUTOFILL_NO
                setText(viaTexts[i])
                addTextChangedListener(object : android.text.TextWatcher {
                    override fun afterTextChanged(w: android.text.Editable?) {
                        if (i < viaTexts.size) viaTexts[i] = w?.toString() ?: ""
                    }
                    override fun beforeTextChanged(c: CharSequence?, a: Int, b: Int, d: Int) = Unit
                    override fun onTextChanged(c: CharSequence?, a: Int, b: Int, d: Int) = Unit
                })
            }
            val del = android.widget.Button(ctx).apply {
                text = getString(R.string.btn_del_via)
                minWidth = 0
                setOnClickListener { removeVia(i) }
            }
            row.addView(tag); row.addView(et); row.addView(del)
            row.setOnLongClickListener { showViaSortMenu(i); true }   /* 长按：排序/删除 */
            binding.viaContainer.addView(row)
        }
        updatePickState()
    }

    private fun addVia() {
        if (viaTexts.size >= MAX_VIA) {
            toast(getString(R.string.via_limit_tip))
            return
        }
        viaTexts.add("")
        viaPoints.add(null)
        viaMarkers.add(null)
        rebuildViaRows()
        log("新增途经点 " + viaTexts.size + "/" + MAX_VIA + "（" + getString(R.string.via_sort_tip) + "）")
    }

    private fun removeVia(index: Int) {
        if (index !in viaTexts.indices) return
        runCatching { viaMarkers.getOrNull(index)?.remove() }
        viaTexts.removeAt(index)
        viaPoints.removeAt(index)
        viaMarkers.removeAt(index)
        rebuildViaRows()
        rebuildViaMarkers()
        log("删除途经点，剩余 " + viaTexts.size + " 个")
    }

    private fun moveVia(from: Int, to: Int) {
        if (from !in viaTexts.indices || to !in viaTexts.indices || from == to) return
        val t = viaTexts.removeAt(from); viaTexts.add(to, t)
        val p2 = viaPoints.removeAt(from); viaPoints.add(to, p2)
        val m = viaMarkers.removeAt(from); viaMarkers.add(to, m)
        rebuildViaRows()
        rebuildViaMarkers()
        log("途经点顺序已调整：" + (from + 1) + " -> " + (to + 1))
    }

    /** 长按途经点行：弹出排序/删除菜单（移动端无右键，用长按替代） */
    private fun showViaSortMenu(index: Int) {
        val items = mutableListOf<String>()
        if (index > 0) items.add(getString(R.string.btn_up))
        if (index < viaTexts.size - 1) items.add(getString(R.string.btn_down))
        items.add(getString(R.string.btn_del_via))
        androidx.appcompat.app.AlertDialog.Builder(this)
            .setTitle(getString(R.string.label_via) + (index + 1))
            .setItems(items.toTypedArray()) { _, which ->
                val choice = items[which]
                when (choice) {
                    getString(R.string.btn_up) -> moveVia(index, index - 1)
                    getString(R.string.btn_down) -> moveVia(index, index + 1)
                    else -> removeVia(index)
                }
            }
            .show()
    }

    /** 地图选点 -> 作为途经点（若已达上限则提示） */
    private fun setViaFromMap(ll: com.amap.api.maps.model.LatLng) {
        if (viaTexts.size >= MAX_VIA) { toast(getString(R.string.via_limit_tip)); return }
        viaTexts.add(String.format(java.util.Locale.US, "%.5f,%.5f", ll.latitude, ll.longitude))
        viaPoints.add(ll)
        viaMarkers.add(null)
        rebuildViaRows()
        rebuildViaMarkers()
        reverseGeocodeOnly(ll) { addr ->
            if (addr.isNotBlank() && viaTexts.isNotEmpty()) {
                viaTexts[viaTexts.size - 1] = addr
                rebuildViaRows()
            }
        }
        log("已添加途经点（地图选点）共 " + viaTexts.size + " 个")
    }

    /** 途经点地图标记（供地图选点后可视化） */
    private fun rebuildViaMarkers() {
        val am = aMap ?: return
        viaMarkers.forEachIndexed { i, m ->
            if (m == null && viaPoints.getOrNull(i) != null) {
                val ll = viaPoints[i]!!
                viaMarkers[i] = runCatching {
                    am.addMarker(
                        com.amap.api.maps.model.MarkerOptions().position(ll)
                            .title(getString(R.string.label_via) + (i + 1))
                    )
                }.getOrNull()
            }
        }
    }

    private fun updatePickState() {
        /* 起终点来源：地图选点 > 输入框地址 > 未选（之前只看地图选点，输入了地址却显示“未选”，是荒谬的） */
        val fromSrc = when {
            startLatLng != null -> "已选(地图)"
            binding.etFrom.text.toString().isNotBlank() -> "已选(地址)"
            else -> "未选"
        }
        val toSrc = when {
            endLatLng != null -> "已选(地图)"
            binding.etTo.text.toString().isNotBlank() -> "已选(地址)"
            else -> "未选"
        }
        binding.tvPickState.text = "起点：" + fromSrc + "　" + "终点：" + toSrc
        /* 摘要行：起终点（地址优先显示，便于一眼确认） */
        val fShow = binding.etFrom.text.toString().trim().ifBlank { if (startLatLng != null) "地图选点" else "—" }
        val tShow = binding.etTo.text.toString().trim().ifBlank { if (endLatLng != null) "地图选点" else "—" }
        val viaN = viaTexts.count { it.isNotBlank() }
        binding.tvRouteSummary.text = "起点 " + fShow +
            (if (viaN > 0) " → 途经 " + viaN + " 处" else "") + " → 终点 " + tShow
        /* 未连接时导航页操作一律不可用；“开始导航/放弃”还需已有预览 */
        val on = client.isConnected
        val hasPreview = previewSource != null
        binding.btnUseMapNav.isEnabled = on
        binding.btnMapClear.isEnabled = on
        binding.btnStartNav.isEnabled = on && hasPreview
        binding.btnGiveUp.isEnabled = on && hasPreview
    }

    /** Toast + 日志（失败原因要看得见，不能只写日志） */
    private fun toast(msg: String) {
        Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
        log(msg)
    }

    private fun ensureMap() {
        if (mapReady) {
            /* 已创建：恢复渲染并强制重新布局（熄屏/切 Tab 后地图可能因尺寸为 0 而空白） */
            runCatching {
                binding.mapView.onResume()
                binding.mapView.requestLayout()
                binding.mapView.postInvalidate()
            }
            return
        }
        try {
            com.amap.api.maps.MapsInitializer.updatePrivacyShow(applicationContext, true, true)
            com.amap.api.maps.MapsInitializer.updatePrivacyAgree(applicationContext, true)
            binding.mapView.onCreate(savedBundle)
            val am = binding.mapView.map ?: throw IllegalStateException("map 对象为空")
            aMap = am
            am.uiSettings.isMyLocationButtonEnabled = true
            am.uiSettings.isZoomControlsEnabled = true
            val st = com.amap.api.maps.model.MyLocationStyle()
            st.myLocationType(com.amap.api.maps.model.MyLocationStyle.LOCATION_TYPE_SHOW)
            st.strokeColor(0xFF000000.toInt())
            st.radiusFillColor(0x2200aa66)
            am.myLocationStyle = st
            am.isMyLocationEnabled = true
            /* 地图样式：确认"淡蓝"真凶是相机飞到海上（定位无效）后，这三项按用户要求加回。
             * 仍放在地图加载完成后应用，最稳。 */
            am.setOnMapLoadedListener {
                runCatching {
                    am.isTrafficEnabled = false      /* 不要路况色带（骑行无关） */
                    am.showBuildings(false)          /* 不要建筑物色块 */
                    am.showIndoorMap(false)          /* 不要室内图 */
                    log("地图样式已应用：关路况/关建筑/关室内图（加载完成后）")
                }
            }
            /* 首次拿到定位后：以当前位置为中心，并缩放到骑行合理范围（方圆约 20~30 公里） */
            am.setOnMyLocationChangeListener { loc ->
                if (loc != null) {
                    val la = loc.latitude
                    val lo = loc.longitude
                    /* 坐标校验：非法或 (0,0) 时绝不移动相机 —— 否则相机会飞到海上（高德海面为淡蓝，
                     * 表现就是整屏一片淡蓝，用户实测过） */
                    val okCoord = la in -90.0..90.0 && lo in -180.0..180.0 &&
                        (kotlin.math.abs(la) > 0.0001 || kotlin.math.abs(lo) > 0.0001)
                    if (!okCoord) {
                        log("⚠ 定位坐标无效(lat=" + la + " lon=" + lo + ")，已忽略，不移动相机")
                        return@setOnMyLocationChangeListener
                    }
                    log("定位更新：lat=" + la + " lon=" + lo)
                }
                if (loc != null && !mapCenteredOnce) {
                    mapCenteredOnce = true
                    runCatching {
                        am.moveCamera(
                            com.amap.api.maps.CameraUpdateFactory.newLatLngZoom(
                                com.amap.api.maps.model.LatLng(loc.latitude, loc.longitude),
                                appPrefs.defaultZoom
                            )
                        )
                    }
                    log("地图已定位到当前位置（缩放级别 " + appPrefs.defaultZoom + "）")
                }
            }
            am.setOnMapClickListener { ll -> askSetPoint(ll) }
            am.setOnMapLongClickListener { ll -> askSetPoint(ll) }   /* 长按也可选点（更灵敏） */
            mapReady = true
            log("地图就绪：点地图可设置起点/终点")
        } catch (t: Throwable) {
            log("! 地图初始化失败：" + t.message)
            log("  若是鉴权失败，请到高德控制台为该应用增加 Android 地图 SDK 类型的 Key")
        }
    }

    private fun askSetPoint(ll: com.amap.api.maps.model.LatLng) {
        androidx.appcompat.app.AlertDialog.Builder(this)
            .setTitle(String.format(java.util.Locale.US, "%.5f, %.5f", ll.latitude, ll.longitude))
            .setItems(arrayOf("设为起点", "设为终点", getString(R.string.menu_set_via))) { _, which ->
                if (which == 0) {
                    startLatLng = ll
                    setMarker(true)
                    reverseGeocode(ll, true)
                } else if (which == 1) {
                    endLatLng = ll
                    setMarker(false)
                    reverseGeocode(ll, false)
                } else {
                    setViaFromMap(ll)
                }
                updatePickState()
            }
            .show()
    }

    private fun setMarker(isStart: Boolean) {
        val am = aMap ?: return
        val ll = (if (isStart) startLatLng else endLatLng) ?: return
        val opt = com.amap.api.maps.model.MarkerOptions().position(ll)
            .title(if (isStart) "起点" else "终点")
        if (isStart) {
            startMarker?.remove()
            startMarker = am.addMarker(opt)
        } else {
            endMarker?.remove()
            endMarker = am.addMarker(opt)
        }
        am.moveCamera(com.amap.api.maps.CameraUpdateFactory.newLatLngZoom(ll, 16f))
    }

    /** 逆地理编码：坐标 -> 地址，回填输入框 */
    private fun reverseGeocode(ll: com.amap.api.maps.model.LatLng, isStart: Boolean) {
        lifecycleScope.launch(Dispatchers.IO) {
            val addr = runCatching {
                val gs = com.amap.api.services.geocoder.GeocodeSearch(applicationContext)
                val q = com.amap.api.services.geocoder.RegeocodeQuery(
                    com.amap.api.services.core.LatLonPoint(ll.latitude, ll.longitude),
                    200f,
                    com.amap.api.services.geocoder.GeocodeSearch.AMAP   // 高德坐标类型
                )
                val addr = gs.getFromLocation(q)
                val c = addr?.city ?: ""
                if (c.isNotBlank() && appPrefs.autoCity && c != appPrefs.geoCity) {
                    appPrefs.geoCity = c
                    log("已按当前定位更新城市：" + c)
                }
                addr?.formatAddress ?: ""
            }.getOrDefault("")
            withContext(Dispatchers.Main) {
                val text = addr.ifEmpty {
                    String.format(java.util.Locale.US, "%.5f,%.5f", ll.latitude, ll.longitude)
                }
                if (isStart) binding.etFrom.setText(text) else binding.etTo.setText(text)
                log((if (isStart) "起点" else "终点") + "：" + text)
            }
        }
    }

    /** 逆地理编码（只取地址文本，不写起终点输入框）—— 供途经点使用 */
    private fun reverseGeocodeOnly(ll: com.amap.api.maps.model.LatLng, onDone: (String) -> Unit) {
        lifecycleScope.launch(Dispatchers.IO) {
            val addr = runCatching {
                val gs = com.amap.api.services.geocoder.GeocodeSearch(applicationContext)
                val q = com.amap.api.services.geocoder.RegeocodeQuery(
                    com.amap.api.services.core.LatLonPoint(ll.latitude, ll.longitude),
                    200f,
                    com.amap.api.services.geocoder.GeocodeSearch.AMAP
                )
                gs.getFromLocation(q)?.formatAddress ?: ""
            }.getOrDefault("")
            val text = addr.ifEmpty {
                String.format(java.util.Locale.US, "%.5f,%.5f", ll.latitude, ll.longitude)
            }
            kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Main) { onDone(text) }
        }
    }

    /** ① 预览路线：只算路、不与 ESP32 同步；成功后在地图上画线等待确认 */
    private fun previewRoute() {
        val s0 = startLatLng
        val e0 = endLatLng
        val fromText = binding.etFrom.text.toString().trim()
        val toText = binding.etTo.text.toString().trim()
        /* 起点/终点：地图选点优先，其次用输入框里的地址文本（两种方式都支持） */
        if (s0 == null && fromText.isEmpty()) {
            toast("请在地图上选起点，或在“起点”输入框填写地址")
            return
        }
        if (e0 == null && toText.isEmpty()) {
            toast("请在地图上选终点，或在“终点”输入框填写地址")
            return
        }
        if (!hasInternet()) {
            toast("手机当前无外网：高德算路需要联网")
            return
        }
        if (!hasLocationPermission()) {
            toast("需要定位权限，请允许后重试")
            ActivityCompat.requestPermissions(
                this,
                arrayOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION),
                REQ_LOCATION_PREVIEW
            )
            return
        }
        /* 途经点：地址 -> 坐标（地图选点已有坐标则直接用）；解析失败即中止 */
        if (viaTexts.any { it.isNotBlank() }) {
            binding.tvRouteInfo.text = "正在解析途经点…"
            lifecycleScope.launch(kotlinx.coroutines.Dispatchers.IO) {
                val gs = com.amap.api.services.geocoder.GeocodeSearch(applicationContext)
                val resolved = mutableListOf<com.espnav.app.data.GeoPoint>()
                for (i in viaTexts.indices) {
                    val txt = viaTexts[i].trim()
                    if (txt.isEmpty()) continue
                    val have = viaPoints.getOrNull(i)
                    if (have != null) {
                        resolved.add(com.espnav.app.data.GeoPoint(have.latitude, have.longitude))
                        continue
                    }
                    val pt = runCatching {
                        gs.getFromLocationName(
                            com.amap.api.services.geocoder.GeocodeQuery(txt, appPrefs.geoCity)
                        )?.firstOrNull()?.latLonPoint
                    }.getOrNull()
                    if (pt == null) {
                        kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Main) {
                            toast("途经点" + (i + 1) + " 解析失败：" + txt)
                            binding.tvRouteInfo.text = getString(R.string.tip_route_info)
                        }
                        return@launch
                    }
                    viaPoints[i] = com.amap.api.maps.model.LatLng(pt.latitude, pt.longitude)
                    resolved.add(com.espnav.app.data.GeoPoint(pt.latitude, pt.longitude))
                }
                kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Main) {
                    startPreviewWithVia(resolved)
                }
            }
            return
        }
        startPreviewWithVia(emptyList())
    }

    /** 实际发起预览算路（wayPoints 已解析成坐标） */
    private fun startPreviewWithVia(vias: List<com.espnav.app.data.GeoPoint>) {
        val s0 = startLatLng
        val e0 = endLatLng
        val fromText = binding.etFrom.text.toString().trim()
        val toText = binding.etTo.text.toString().trim()
        previewSource?.stop()
        val src = AmapNavSource(
            applicationContext,
            if (s0 != null) "" else fromText,      /* 地图选点优先，否则用地址文本 */
            if (e0 != null) "" else toText,
            appPrefs.geoCity, emulate = true,
            fixedFrom = s0?.let { com.espnav.app.data.GeoPoint(it.latitude, it.longitude) },
            fixedTo = e0?.let { com.espnav.app.data.GeoPoint(it.latitude, it.longitude) },
            wayPoints = vias
        )
        src.logSink = { msg -> runOnUiThread { log("高德: " + msg) } }
        src.onRouteReady = { len, sec, coords -> runOnUiThread { showRoutePreview(len, sec, coords) } }
        previewSource = src
        binding.tvRouteInfo.text = "正在算路…"
        binding.btnStartNav.visibility = View.GONE
        binding.btnGiveUp.visibility = View.GONE
        src.start()
        log("正在算路（预览模式，不会发送到 ESP32）…")
        /* 超时兜底：高德导航回调若一直不返回，不能让你傻等（最常见原因是 Key 未开通导航服务） */
        routeTimeoutJob?.cancel()
        routeTimeoutJob = lifecycleScope.launch {
            delay(ROUTE_TIMEOUT_MS)
            if (binding.tvRouteInfo.text.startsWith("正在算路")) {
                binding.tvRouteInfo.text = "算路超时 ✕"
                toast("算路超时：请检查 ①高德Key是否开通『Android导航SDK』服务 ②手机是否有外网")
                log("⚠ 算路超时（" + (ROUTE_TIMEOUT_MS / 1000) + " 秒无回调）。常见原因：")
                log("   ① 高德 Key 只开通了地图服务，未开通【Android 导航 SDK】——去高德控制台确认")
                log("   ② 手机当前无外网（如连的是 ESPNav-AP 热点）")
                log("   ③ 起终点距离过近/无骑行路径可算")
            }
        }
    }

    /** 预览成功：地图画蓝色路线 + 显示全程/时间 + 出现「开始导航/放弃」 */
    private fun showRoutePreview(len: Int, sec: Int, coords: List<com.espnav.app.data.GeoPoint>) {
        routeTimeoutJob?.cancel()
        routeTimeoutJob = null
        val am = aMap ?: return
        runCatching { previewLine?.remove() }
        val pts = coords.map { com.amap.api.maps.model.LatLng(it.lat, it.lon) }
        if (pts.isNotEmpty()) {
            previewLine = am.addPolyline(
                com.amap.api.maps.model.PolylineOptions()
                    .addAll(pts)
                    .width(12f)
                    .color(0xCC1E88E5.toInt())
            )
            runCatching {
                val b = com.amap.api.maps.model.LatLngBounds.builder()
                pts.forEach { b.include(it) }
                am.moveCamera(com.amap.api.maps.CameraUpdateFactory.newLatLngBounds(b.build(), 60))
            }
        }
        previewLenM = len
        previewSecS = sec
        val info = String.format(java.util.Locale.US, "全程 %.1f km · 预计 %d 分钟", len / 1000.0, sec / 60)
        binding.tvRouteInfo.text = info
        binding.btnStartNav.visibility = View.VISIBLE
        binding.btnGiveUp.visibility = View.VISIBLE
        log("路线预览：" + info + " → 确认请点「开始导航」")
    }

    /** ② 确认后：启动导航并与 ESP32 同步 */
    private fun startConfirmedNav() {
        val src = previewSource
        if (src == null) {
            toast("请先点「预览路线」")
            return
        }
        if (!client.isConnected) {
            toast("未连接，无法开始导航")
            return
        }
        src.onRouteReady = null
        src.startNavigation()
        launchNav(src, "高德骑行导航(地图选点)")
        binding.btnStartNav.visibility = View.GONE
        binding.btnGiveUp.visibility = View.GONE
        /* 状态栏：不要停留在“尚未算路”（用户反馈不合理） */
        binding.tvRouteInfo.text = String.format(
            java.util.Locale.US, "导航中 · 全程 %.1f km · 预计 %d 分钟",
            previewLenM / 1000.0, previewSecS / 60
        )
        log("已确认路线，开始与 ESP32 同步导航数据")
    }

    /** 放弃预览：移除折线、停止数据源 */
    private fun giveUpPreview() {
        runCatching { previewLine?.remove() }
        previewLine = null
        previewSource?.stop()
        previewSource = null
        binding.tvRouteInfo.text = getString(R.string.tip_route_info)
        updatePickState()
        binding.btnStartNav.visibility = View.GONE
        binding.btnGiveUp.visibility = View.GONE
        log("已放弃本次路线预览")
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
        if (binding.tvLog.lineCount > appPrefs.maxLogLines) {
            val all = binding.tvLog.text.toString()
            binding.tvLog.text = all.substring(all.length / 3)   // 超限时丢弃最早 1/3
        }
        binding.svLog.post { binding.svLog.fullScroll(View.FOCUS_DOWN) }
    }

    companion object {
        private const val DEFAULT_HOST = "192.168.4.1"
        private const val REQ_LOCATION = 1001          /* 连接页：高德骑行导航 */
        private const val REQ_LOCATION_PREVIEW = 1003  /* 导航页：预览路线（必须与上面区分，否则授权后会误启导航） */
        private const val REQ_NOTIFY = 1002
        /** 地图默认缩放级别：11 ≈ 方圆 20~30 公里（适合骑行选点） */
        private const val DEFAULT_ZOOM = 11f
        private const val MAX_RECONNECT = 5
        private const val RECONNECT_DELAY_MS = 3000L
        /** 默认测试起终点（骑行；emulate=true 为模拟行进，室内也可测） */
        private const val FROM_ADDRESS = "北京亦庄泰河三街1号"
        private const val TO_ADDRESS = "北京亦庄同济南路地铁站"
        private const val KEY_LAST_IP = "last_ip"
        private const val FRAME_INTERVAL_MS = 200L   // 5 Hz（协议上限 10fps）
        private const val ROUTE_TIMEOUT_MS = 12000L  // 算路超时兜底（毫秒）
        private const val MAX_VIA = 3                // 途经点上限（与高德/百度一致）
        private const val MAX_LOG_LINES = 1000
    }
}
