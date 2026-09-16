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
    private val emulate: Boolean = true
) : NavSource, SimpleNaviListener() {

    companion object {
        private const val TAG = "AmapNavSource"
        /** toAddress 为空时使用的自动目的地：当前位置北向 N 米 */
        private const val AUTO_DEST_METERS = 2000
        /** 路径点采样上限（协议单帧点数上限 16） */
        private const val MAX_PATH_PTS = 16
        /** 主视图显示“前方多少米”的路径（决定路面/绿线的缩放） */
        private const val VIEW_METERS = 400.0
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

    /** 真实全程（米），取自 AMapNaviPath.allLength */
    @Volatile
    private var totalMeters: Int = 0

    private val etaFmt = SimpleDateFormat("HH:mm", Locale.getDefault())

    /** 日志出口：MainActivity 接到界面日志区，便于真机诊断（不必连电脑抓 logcat） */
    var logSink: ((String) -> Unit)? = null

    private fun log(msg: String) {
        log(msg)
        logSink?.invoke(msg)
    }

    private fun logE(msg: String) {
        logE(msg)
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
            n.addAMapNaviListener(this)
            started = true
            log("AMapNavi 初始化完成 起点=" + fromAddress.ifBlank { "当前定位" } +
                " 终点=" + toAddress + " 模拟行进=" + emulate)
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
        if (toAddress.isNotBlank()) {
            geocodeAndRoute()
        } else {
            log("终点为空：等待定位后使用自动目的地（北向 " + AUTO_DEST_METERS + " 米）")
        }
    }

    override fun onInitNaviFailure() {
        logE("onInitNaviFailure：初始化失败（检查 Key / 包名 / SHA1 / 网络）")
    }

    /** 地址 -> 坐标 -> 骑行算路（网络请求放在后台线程） */
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
        try {   // onCalculateRouteSuccess 加固
        // 取真实路径（坐标列表）与全程距离
        try {
            val path = navi?.naviPath
            if (path != null) {
                totalMeters = path.allLength
                val raw = path.coordList ?: emptyList()
                pathCoords = downsample(raw.map { GeoPoint(it.latitude, it.longitude) }, MAX_PATH_PTS)
                state = state.copy(totalDistMeters = totalMeters)
                log("路径点 " + raw.size + " -> 采样 " + pathCoords.size +
                    "；全程 " + totalMeters + " 米，预计 " + path.allTime + " 秒")
            }
        } catch (e: Exception) {
            log("读取路径失败：" + e.message)
        }
        log("算路成功，启动导航（模拟行进=" + emulate + "）")
        try {
            val ok = navi?.startNavi(if (emulate) NaviType.EMULATOR else NaviType.GPS) ?: false
            log("startNavi 返回 " + ok + "（模拟行进=" + emulate + "）")
            if (!ok) logE("导航启动失败：检查高德 Key 是否绑定包名 com.espnav.app / SHA1，且类型为 Android 导航 SDK")
        } catch (e: Exception) {
            logE("startNavi 异常：" + e.message)
        }
        } catch (t: Throwable) { logE("onCalculateRouteSuccess 异常：" + t.message) }
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
        // 起点用当前定位且尚未算路：拿到首个定位后开始算路
        if (!routeRequested && fromAddress.isBlank() && c != null) {
            routeRequested = true
            if (toAddress.isNotBlank()) geocodeAndRoute() else calculateAutoDest()
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
        if (pathCoords.isEmpty()) return
        val origin = lastOrigin ?: pathCoords.first()

        // 主视图：只投影“前方 VIEW_METERS 米内”的路径，缩放自适应该视野（近端 y=144）
        val near = pathCoords.filter { metersBetween(origin, it) <= VIEW_METERS }
            .ifEmpty { listOf(pathCoords.first()) }
        val pxPerMeter = (NavStateMapper.NEAR_Y - NavStateMapper.FAR_Y).toDouble() / VIEW_METERS
        val screen = NavStateMapper.project(near, origin, state.headingDeg, pxPerMeter = pxPerMeter)
        // 小地图：整条路线，北向上，按包围盒自适应
        state = state.copy(
            remainPath = screen,
            passedPath = screen.take(2),
            overviewPath = NavStateMapper.miniMapFromGeo(pathCoords)
        )
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
