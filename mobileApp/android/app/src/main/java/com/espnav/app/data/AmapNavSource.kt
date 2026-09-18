package com.espnav.app.data

import android.content.Context
import android.util.Log
import com.amap.api.maps.MapsInitializer
import com.amap.api.navi.AMapNavi
import com.amap.api.navi.SimpleNaviListener
import com.amap.api.navi.enums.IconType
import com.amap.api.navi.enums.NaviType
import com.amap.api.navi.model.AMapCalcRouteResult
import com.amap.api.navi.model.AMapNaviLocation
import com.amap.api.navi.model.NaviInfo
import com.amap.api.navi.model.NaviLatLng
import com.amap.api.services.core.LatLonPoint
import com.amap.api.services.geocoder.GeocodeQuery
import com.amap.api.services.geocoder.GeocodeSearch
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * 高德导航数据源（AMapNavi 11.2.100）——骑行模式。
 *
 * 起终点支持两种方式：
 *  1. 地址文本（用高德地理编码 GeocodeSearch 解析成坐标）—— 默认使用（例如泰河三街1号 -> 同济南路地铁站）
 *  2. 起点留空 = 使用当前定位
 * 默认 [emulate] = true（NaviType.EMULATOR，模拟行进）：**室内也能看到屏幕随路线推进**；
 * 改为 false 则是实时 GPS 导航（需在室外行驶）。
 */
class AmapNavSource(
    private val context: Context,
    private val fromAddress: String,
    private val toAddress: String,
    private val city: String = "北京",
    private val emulate: Boolean = true,
    /** 地图选点直接给定的起终点（非空时优先使用，跳过地址解析） */
    private val fixedFrom: GeoPoint? = null,
    private val fixedTo: GeoPoint? = null,
    /** 途经点（按顺序生效，最多 3 个；骑行算路原生支持 wayPoints） */
    private val wayPoints: List<GeoPoint> = emptyList(),
    /** 行程图采样方式：PolylineSampler.MODE_VW（默认）/ MODE_DP，由设置页决定 */
    private val samplerMode: String = PolylineSampler.MODE_VW
) : NavSource, SimpleNaviListener() {

    companion object {
        private const val TAG = "AmapNavSource"
        /** toAddress 为空时使用的自动目的地：当前位置北向 N 米 */
        private const val AUTO_DEST_METERS = 2000
        /** 算路策略：0 = 高德默认 */
        private const val ROUTE_STRATEGY_DEFAULT = 0
        /** 路径点采样上限（协议单帧点数上限 16） */
        private const val MAX_PATH_PTS = 16
        /** 地图画线用的最大点数（全量路径可能有几千点，直接画会卡顿/闪退） */
        private const val MAX_PREVIEW_PTS = 200
        /** 主视图显示“前方多少米”的路径（决定路面/绿线的缩放） */
        private const val VIEW_METERS = 400.0
        /** 车身后方额外保留的路径长度（米），用于"已走过"绿线 */
        private const val BEHIND_METERS = 60.0

        /** AMapNavi 是**进程级单例**，而 onInitNaviSuccess() 只在首次初始化成功时回调一次：
         *  第二次 getInstance() 不会再触发 —— 于是算路永不发起，表现为"导航一次后再点启动毫无反应，
         *  必须重启 App"（用户实测）。用这个进程内标记在"已初始化过"时直接算路，不再依赖该回调。 */
        @Volatile
        private var sNaviInited = false
    }

    private var navi: AMapNavi? = null
    private var started = false
    private var routeRequested = false

    @Volatile
    private var state: NavState = NavState(turnType = TurnType.UNKNOWN)

    @Volatile
    private var lastOrigin: GeoPoint? = null

    /** 高德给出的真实路径（经纬度，已降采样）；用于屏幕路形、绿色路径线与小地图 */
    @Volatile
    private var pathCoords: List<GeoPoint> = emptyList()

    /** 全量路径坐标（供地图画线预览，不降采样） */
    @Volatile
    private var pathAllCoords: List<GeoPoint> = emptyList()

    /** 预览模式回调：算路成功但**不启动导航**，把路线交给界面预览确认 */
    var onRouteReady: ((lengthMeters: Int, timeSec: Int, coords: List<GeoPoint>) -> Unit)? = null

    /** 真实全程（米），取自 AMapNaviPath.allLength */
    @Volatile
    private var totalMeters: Int = 0

    private val etaFmt = SimpleDateFormat("HH:mm", Locale.getDefault())

    /** 日志出口：MainActivity 接到界面日志区，便于真机诊断（不必连电脑抓 logcat） */
    var logSink: ((String) -> Unit)? = null

    private fun log(msg: String) {
        Log.i(TAG, msg)
        logSink?.invoke(msg)
    }

    private fun logE(msg: String) {
        Log.e(TAG, msg)
        logSink?.invoke("! " + msg)
    }

    override val displayName: String
        get() = if (emulate) "高德骑行(模拟行进)" else "高德骑行(实时)"

    // ---------------- 生命周期 ----------------

    override fun start() {
        if (started) return
        try {
            // 合规：必须在初始化/定位前声明隐私政策
            MapsInitializer.updatePrivacyShow(context, true, true)
            MapsInitializer.updatePrivacyAgree(context, true)

            val n = AMapNavi.getInstance(context)
            navi = n
            runCatching { n.removeAMapNaviListener(this) }   /* 幂等：避免重复注册导致回调重复/串实例 */
            n.addAMapNaviListener(this)
            started = true
            log("AMapNavi 初始化完成 起点=" + fromAddress.ifBlank { "当前定位" } +
                " 终点=" + toAddress + " 模拟行进=" + emulate)
            /* 单例已就绪过 → onInitNaviSuccess() 不会再回调，必须自己发起算路 */
            if (sNaviInited) {
                log("导航 SDK 已就绪过（单例复用）→ 直接发起算路")
                beginRoute()
            }
        } catch (e: Exception) {
            logE("初始化失败：" + e.message)
        }
    }

    override fun stop() {
        try {
            navi?.stopNavi()
            navi?.removeAMapNaviListener(this)
        } catch (e: Exception) {
            log("stop 异常：" + e.message)
        } finally {
            started = false
        }
    }

    override fun latest(): NavState = state

    // ---------------- 算路 ----------------

    override fun onInitNaviSuccess() {
        log("onInitNaviSuccess：SDK 就绪")
        sNaviInited = true
        beginRoute()
    }

    /** 真正发起算路 —— onInitNaviSuccess（首次初始化）与"单例复用"两条路径共用同一份逻辑 */
    private fun beginRoute() {
        if (fixedTo != null) {
            log("使用地图选点坐标算路：起点=" + (fixedFrom?.toString() ?: "当前定位") + " 终点=" + fixedTo)
            calculateRoute(fixedFrom)
        } else if (toAddress.isNotBlank()) {
            geocodeAndRoute()
        } else {
            log("终点为空：等待定位后使用自动目的地（北向 " + AUTO_DEST_METERS + " 米）")
        }
    }

    override fun onInitNaviFailure() {
        logE("onInitNaviFailure：初始化失败（检查 Key / 包名 / SHA1 / 网络）")
    }

    /** 地址 -> 坐标 -> 骑行算路（网络请求放在后台线程） */
    /** 发起算路：起点=给定点或当前定位，终点=地图选点坐标（fixedTo）
     *  注意：本项目是**骑行**导航，必须用 calculateRideRoute；此前这里误用 calculateDriveRoute（驾车），
     *  与连接页的骑行导航不一致，且会导致发起后回调不返回（界面卡在“正在算路…”）。 */
    fun calculateRoute(from: GeoPoint?) {
        val n = navi ?: return
        val start = from ?: lastOrigin
        if (start == null) {
            log("尚无定位，无法算路")
            return
        }
        val to = fixedTo
        if (to == null) {
            log("无目的地坐标：请先在地图上选终点，或使用地址输入")
            return
        }
        /* 有途经点时改用 NaviPoi 版骑行算路（com.amap.api.navi.AMapNavi#calculateRideRoute 的
         * (NaviPoi, List<NaviPoi>, NaviPoi, TravelStrategy) 重载，原生支持 wayPoints） */
        val ok = if (wayPoints.isEmpty()) {
            n.calculateRideRoute(
                NaviLatLng(start.lat, start.lon),
                NaviLatLng(to.lat, to.lon)
            )
        } else {
            n.calculateRideRoute(
                com.amap.api.navi.model.NaviPoi(
                    "起点", com.amap.api.maps.model.LatLng(start.lat, start.lon), ""),
                wayPoints.map {
                    com.amap.api.navi.model.NaviPoi(
                        "途经点", com.amap.api.maps.model.LatLng(it.lat, it.lon), "")
                },
                com.amap.api.navi.model.NaviPoi(
                    "终点", com.amap.api.maps.model.LatLng(to.lat, to.lon), ""),
                com.amap.api.navi.enums.TravelStrategy.SINGLE
            )
        }
        log("发起骑行算路 result=" + ok + " 途经点=" + wayPoints.size +
            " 起点=(" + start.lat + ", " + start.lon + ") 终点=(" + to.lat + ", " + to.lon + ")")
    }

    private fun geocodeAndRoute() {
        Thread {
            try {
                val searcher = GeocodeSearch(context)
                val to = geocode(searcher, toAddress)
                if (to == null) {
                    logE("终点地址解析失败：" + toAddress)
                    return@Thread
                }
                val from = if (fromAddress.isBlank()) null else geocode(searcher, fromAddress)
                val start = from ?: lastOrigin?.let { NaviLatLng(it.lat, it.lon) }
                if (start == null) {
                    log("起点未解析且暂无定位，等待下次定位后重试")
                    routeRequested = false
                    return@Thread
                }
                val ok = navi?.calculateRideRoute(start, to) ?: false
                log("发起骑行算路 result=" + ok + " 起点=" + fromAddress.ifBlank { "当前定位" } +
                    " 终点=" + toAddress)
            } catch (e: Exception) {
                logE("地理编码/算路异常：" + e.message)
            }
        }.start()
    }

    private fun geocode(searcher: GeocodeSearch, address: String): NaviLatLng? {
        val list = searcher.getFromLocationName(GeocodeQuery(address, city)) ?: return null
        val first = list.firstOrNull() ?: return null
        val p: LatLonPoint = first.latLonPoint ?: return null
        log("地址解析：" + address + " -> (" + p.latitude + ", " + p.longitude + ") " + first.formatAddress)
        return NaviLatLng(p.latitude, p.longitude)
    }

    // ---------------- 高德回调 -> NavState ----------------

    override fun onCalculateRouteSuccess(result: AMapCalcRouteResult?) {
        try {
            val path = navi?.naviPath
            var len = 0
            var sec = 0
            if (path != null) {
                len = path.allLength
                sec = path.allTime
                totalMeters = len
                val raw = path.coordList ?: emptyList()
                pathAllCoords = raw.map { GeoPoint(it.latitude, it.longitude) }
                /* 行程图采样：按几何重要性取点（VW/DP 由设置决定），替代原先的等间隔降采样 ——
                 * 等间隔会在长直段浪费点数，并可能整段跳过某个拐弯（用户实测"拐弯不准"）。 */
                pathCoords = PolylineSampler.sampleGeo(pathAllCoords, samplerMode, MAX_PATH_PTS)
                state = state.copy(totalDistMeters = len)
                log("路径点 " + raw.size + " -> 采样 " + pathCoords.size +
                    "；全程 " + len + " 米，预计 " + sec + " 秒")
            }
            val cb = onRouteReady
            if (cb != null) {
                log("预览模式：路线已算出，等待确认后再启动导航")
                cb(len, sec, downsample(pathAllCoords, MAX_PREVIEW_PTS))
            } else {
                startNavigation()
            }
        } catch (t: Throwable) {
            logE("onCalculateRouteSuccess 异常：" + t.message)
        }
    }

    /** 确认后真正启动导航（预览模式由界面调用；非预览模式在算路成功时自动调用） */
    fun startNavigation() {
        try {
            val ok = navi?.startNavi(if (emulate) NaviType.EMULATOR else NaviType.GPS) ?: false
            log("startNavi 返回 " + ok + "（模拟行进=" + emulate + "）")
            if (!ok) {
                logE("导航启动失败：检查高德 Key 是否绑定包名 com.espnav.app / SHA1，且类型为 Android 导航 SDK")
            }
        } catch (e: Exception) {
            logE("startNavi 异常：" + e.message)
        }
    }

    override fun onCalculateRouteFailure(errorCode: Int) {
        logE("算路失败 errorCode=" + errorCode + "（网络/起终点/Key 权限）")
    }

    override fun onLocationChange(loc: AMapNaviLocation?) {
        try {   // onLocationChange 加固
        val l = loc ?: return
        val c = l.coord
        if (c != null) lastOrigin = GeoPoint(c.latitude, c.longitude)
        state = state.copy(
            headingDeg = l.bearing.toInt(),
            speedKmh = (l.speed * 3.6f).toInt()          // Location.speed 单位 m/s
        )
        /* 只有"起点用定位 + 终点用地址"这一种情形，才在拿到首个定位后自动算路。
         * 之前漏判 fixedTo/fixedFrom：地图选点时 fromAddress/toAddress 都为空，
         * 会误走 calculateAutoDest()，算出一条"当前位置向北 2km"的假路线（表现为算路不对/超时）。 */
        if (!routeRequested && c != null &&
            fixedTo == null && fixedFrom == null &&
            fromAddress.isBlank() && toAddress.isNotBlank()
        ) {
            routeRequested = true
            geocodeAndRoute()
        }
        refreshPathProjection()
        } catch (t: Throwable) { logE("onLocationChange 异常：" + t.message) }
    }

    /** 等间隔降采样（保留首尾），控制发帧体积 */
    private fun downsample(pts: List<GeoPoint>, maxN: Int): List<GeoPoint> {
        if (pts.size <= maxN || maxN < 2) return pts
        val out = ArrayList<GeoPoint>(maxN)
        for (i in 0 until maxN) {
            out.add(pts[(i.toDouble() * (pts.size - 1) / (maxN - 1)).toInt()])
        }
        return out
    }

    /**
     * 把真实路径投影成：① 车头朝上的屏幕坐标（画路面/绿线）② 北向上的小地图局部坐标。
     * 位置/朝向更新时调用。
     */
    private fun refreshPathProjection() {
        if (pathAllCoords.isEmpty()) return
        val origin = lastOrigin ?: pathAllCoords.first()

        /* 【修复·主视图折线根因】此前用【整条路线的 16 点降采样】去过滤"前方 400 m"：
         * 5 km 路线点距约 333 m，400 m 内常常只剩 1~2 点，于是路面绿线又粗又飘。
         * 现在改为：先在【全量路径】上定位离车最近的索引 i0，再沿路径顺序向前/向后取点 ——
         * 取出的点必然连续且顺序正确（不再出现"车前车后混在一起、横穿屏幕"的错线）。
         */
        val i0 = nearestIndex(origin)

        /* 前方：沿路径前进，累计到 VIEW_METERS 米 */
        val aheadRaw = ArrayList<GeoPoint>()
        aheadRaw.add(pathAllCoords[i0])
        var acc = 0.0
        var j = i0
        while (j + 1 < pathAllCoords.size && acc < VIEW_METERS) {
            acc += metersBetween(pathAllCoords[j], pathAllCoords[j + 1])
            j++
            aheadRaw.add(pathAllCoords[j])
        }

        /* 车身后方一小段：用于"已走过"线（投影后一般落在屏幕下沿之外，不靠贴边造线） */
        val behindRaw = ArrayList<GeoPoint>()
        var b = i0
        var bacc = 0.0
        while (b - 1 >= 0 && bacc < BEHIND_METERS) {
            bacc += metersBetween(pathAllCoords[b], pathAllCoords[b - 1])
            b--
            behindRaw.add(0, pathAllCoords[b])
        }

        /* 到达终点附近时"前方"可能只剩 1 个点 → 固件因 center_n < 2 而整段不画路面
         * （用户实测"最后一个路段道路消失、导航结束后路面完全没了"）。这里补足到 2 点。 */
        if (aheadRaw.size < 2) {
            val a = aheadRaw[0]
            val b = behindRaw.lastOrNull()
            if (b != null && (b.lat != a.lat || b.lon != a.lon)) aheadRaw.add(0, b)
            else aheadRaw.add(origin)
        }

        /* 【投影基准】用【车头朝向 bearing】：这样"车头前方的路"始终在屏幕上方，路的左右弯曲才真实
         * （与真实导航屏一致）。曾一度改用"路径切线"，会让路永远笔直向上、看不出前方转弯，
         * 而且那两条折线已按用户要求删除，不再有"随车头旋转的折线"问题。 */
        val baseHeading = state.headingDeg
        val pxPerMeter = (NavStateMapper.NEAR_Y - NavStateMapper.FAR_Y).toDouble() / VIEW_METERS
        val screen = NavStateMapper.project(squeezeToLimit(aheadRaw), origin, baseHeading, pxPerMeter = pxPerMeter)
        val past = NavStateMapper.project(squeezeToLimit(behindRaw), origin, baseHeading, pxPerMeter = pxPerMeter)

        // 小地图：整条路线（采样后的 <=16 点），北向上，按包围盒等比自适应（含 cos 纬度折算）
        val proj = NavStateMapper.overviewProjectorFor(pathCoords)
        state = state.copy(
            remainPath = screen,
            passedPath = past,
            overviewPath = proj?.let { pr -> pathCoords.map { pr.project(it) } } ?: emptyList(),
            /* 黄点：用【当前定位】直接投影到行程图坐标系（与路径点同一套变换） */
            overviewDotPos = proj?.project(origin)
        )
    }

    /** 路径在索引 i 处的前进方向（度，0=北、顺时针）；用前后各 3 点求方向，避免单点抖动 */
    private fun pathHeadingAt(i: Int): Int {
        val n = pathAllCoords.size
        if (n < 2) return state.headingDeg
        val a = pathAllCoords[(i - 3).coerceAtLeast(0)]
        val b = pathAllCoords[(i + 3).coerceAtMost(n - 1)]
        val dLat = b.lat - a.lat
        val dLon = (b.lon - a.lon) * kotlin.math.cos(Math.toRadians(a.lat))
        if (kotlin.math.abs(dLat) < 1e-12 && kotlin.math.abs(dLon) < 1e-12) return state.headingDeg
        val deg = Math.toDegrees(Math.atan2(dLon, dLat))
        return ((deg.toInt() % 360) + 360) % 360
    }

    /** 离当前位置最近的路径点索引（路径点通常 < 2000，线性扫描足够） */
    private fun nearestIndex(origin: GeoPoint): Int {
        var best = 0
        var bestD = Double.MAX_VALUE
        for (i in pathAllCoords.indices) {
            val d = metersBetween(origin, pathAllCoords[i])
            if (d < bestD) { bestD = d; best = i }
        }
        return best
    }

    /** 把一段路径压到协议单帧上限（MAX_PATH_PTS）以内：超过则用 DP（保留局部形状） */
    private fun squeezeToLimit(pts: List<GeoPoint>): List<GeoPoint> {
        if (pts.size <= MAX_PATH_PTS) return pts
        val o = pts.first()
        val meters = PolylineSampler.toMeters(pts, o)
        return PolylineSampler.toGeo(PolylineSampler.simplifyDp(meters, MAX_PATH_PTS), o)
    }

    /* ---------------- 供导航态地图使用（相机跟随 / 分色 / 车头箭头 / 行程图卡片）---------------- */

    /** 当前定位；尚未拿到定位时返回 null */
    fun currentOrigin(): GeoPoint? = lastOrigin

    /** 当前车头朝向（度，0=北，顺时针） */
    fun currentHeading(): Int = state.headingDeg

    /** 当前转向提示（与发往 ESP32 的 hint 完全一致） */
    fun currentHint(): String = NavStateMapper.hintOf(state)

    /** 剩余距离（米） */
    fun currentRemainMeters(): Int = state.remainDistMeters

    /** 整条路线（全量坐标） */
    fun fullPath(): List<GeoPoint> = pathAllCoords

    /** 当前定位在整条路线上的最近点索引（用于"已走/未走"分色与行程图黄点） */
    fun currentPathIndex(): Int {
        val o = lastOrigin ?: return 0
        var best = 0
        var bestD = Double.MAX_VALUE
        for (i in pathAllCoords.indices) {
            val d = metersBetween(o, pathAllCoords[i])
            if (d < bestD) { bestD = d; best = i }
        }
        return best
    }

    /** 两点间近似距离（米） */
    private fun metersBetween(a: GeoPoint, b: GeoPoint): Double {
        val dLat = (b.lat - a.lat) * 111_320.0
        val dLon = (b.lon - a.lon) * 111_320.0 * kotlin.math.cos(Math.toRadians(a.lat))
        return kotlin.math.sqrt(dLat * dLat + dLon * dLon)
    }

    /** toAddress 为空时的兜底：当前位置 -> 北向 N 米 */
    private fun calculateAutoDest() {
        val n = navi ?: return
        val o = lastOrigin ?: return
        val to = NaviLatLng(o.lat + AUTO_DEST_METERS / 111_320.0, o.lon)
        val ok = n.calculateRideRoute(NaviLatLng(o.lat, o.lon), to)
        log("自动目的地算路 result=" + ok)
    }

    override fun onNaviInfoUpdate(info: NaviInfo?) {
        try {   // onNaviInfoUpdate 加固
        val i = info ?: return
        state = state.copy(
            turnType = mapIcon(i.iconType),
            turnDistMeters = i.curStepRetainDistance,
            remainDistMeters = i.pathRetainDistance,
            totalDistMeters = if (state.totalDistMeters > 0) state.totalDistMeters else i.pathRetainDistance,
            currentRoad = i.currentRoadName ?: "",
            nextRoad = i.nextRoadName ?: "",
            speedKmh = if (i.currentSpeed > 0) i.currentSpeed else state.speedKmh,  // NaviInfo 单位 km/h
            elapsedSec = state.elapsedSec + 1,
            etaText = etaTextOf(i.pathRetainTime)
        )
        } catch (t: Throwable) { logE("onNaviInfoUpdate 异常：" + t.message) }
    }

    /** 剩余秒 -> 预计到达时刻（HH:mm） */
    private fun etaTextOf(remainSeconds: Int): String {
        if (remainSeconds <= 0) return state.etaText
        return runCatching {
            etaFmt.format(Date(System.currentTimeMillis() + remainSeconds * 1000L))
        }.getOrDefault(state.etaText)
    }

    override fun onArriveDestination() {
        log("到达目的地")
        state = state.copy(turnType = TurnType.ARRIVE, turnDistMeters = 0, remainDistMeters = 0)
    }

    override fun onEndEmulatorNavi() {
        log("模拟行进结束")
    }

    /** 高德转向图标 -> 统一 TurnType */
    private fun mapIcon(icon: Int): TurnType = when (icon) {
        IconType.STRAIGHT, IconType.DEFAULT, IconType.SPECIAL_CONTINUE -> TurnType.STRAIGHT
        IconType.LEFT_FRONT -> TurnType.SLIGHT_LEFT
        IconType.LEFT -> TurnType.LEFT
        IconType.LEFT_BACK -> TurnType.SHARP_LEFT
        IconType.RIGHT_FRONT -> TurnType.SLIGHT_RIGHT
        IconType.RIGHT -> TurnType.RIGHT
        IconType.RIGHT_BACK -> TurnType.SHARP_RIGHT
        IconType.LEFT_TURN_AROUND, IconType.U_TURN_RIGHT -> TurnType.UTURN
        IconType.ENTER_ROUNDABOUT, IconType.OUT_ROUNDABOUT,
        IconType.ENTRY_LEFT_RING, IconType.LEAVE_LEFT_RING,
        IconType.ENTRY_RING_LEFT, IconType.ENTRY_RING_RIGHT -> TurnType.ROUNDABOUT
        IconType.ARRIVED_DESTINATION -> TurnType.ARRIVE
        else -> TurnType.STRAIGHT
    }
}
