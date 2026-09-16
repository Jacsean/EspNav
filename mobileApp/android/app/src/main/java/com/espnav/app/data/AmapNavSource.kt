package com.espnav.app.data

import android.content.Context
import android.util.Log
import com.amap.api.maps.MapsInitializer
import com.amap.api.navi.AMapNavi
import com.amap.api.navi.SimpleNaviListener
import com.amap.api.navi.enums.IconType
import com.amap.api.navi.model.AMapCalcRouteResult
import com.amap.api.navi.model.AMapNaviLocation
import com.amap.api.navi.model.NaviInfo
import com.amap.api.navi.model.NaviLatLng

/**
 * 高德导航数据源（AMapNavi 11.2.100）。
 *
 * 职责：初始化 SDK → 起终点算路 → 启动导航 → 把导航回调转成统一的 [NavState]。
 * 屏幕端与协议层完全不感知高德的存在（见 [NavSource] / [NavStateMapper]）。
 *
 * 说明：起终点先用"当前位置 → 指定目的地"（目的地由界面传入），后续可换成地址搜索选点。
 */
class AmapNavSource(
    private val context: Context,
    private val destLat: Double,
    private val destLon: Double,
    /** >0 时忽略 destLat/destLon，改用“当前位置北向前方 N 米”作为目的地（用于快速验证链路） */
    private val autoDestMeters: Int = 0
) : NavSource, SimpleNaviListener() {

    companion object {
        private const val TAG = "AmapNavSource"

        /** 算路策略：0 = 高德默认（驾车）。骑行可后续改为对应策略常量。 */
        private const val ROUTE_STRATEGY_DEFAULT = 0

        /** 导航启动模式：1 = GPS 实时导航（高德 AMapNavi 常用取值） */
        private const val NAVI_MODE_GPS = 1
    }

    private var navi: AMapNavi? = null
    private var started = false
    private var routeRequested = false

    /** 导航回调缓存的最新状态（[latest] 直接返回它，不推进） */
    @Volatile
    private var state: NavState = NavState(turnType = TurnType.UNKNOWN)

    /** 最近一次定位（用作路径投影原点） */
    @Volatile
    private var lastOrigin: GeoPoint? = null

    override val displayName: String get() = "高德导航"

    // ---------------- 生命周期 ----------------

    override fun start() {
        if (started) return
        try {
            // 合规：必须在初始化与定位前声明隐私政策（否则 SDK 拒绝工作）
            MapsInitializer.updatePrivacyShow(context, true, true)
            MapsInitializer.updatePrivacyAgree(context, true)

            val n = AMapNavi.getInstance(context)
            navi = n
            n.addAMapNaviListener(this)
            started = true
            Log.i(TAG, "AMapNavi 初始化完成，等待 onInitNaviSuccess 后算路")
        } catch (e: Exception) {
            Log.e(TAG, "初始化失败：${e.message}")
        }
    }

    override fun stop() {
        try {
            navi?.stopNavi()
            navi?.removeAMapNaviListener(this)
        } catch (e: Exception) {
            Log.w(TAG, "stop 异常：${e.message}")
        } finally {
            started = false
        }
    }

    override fun latest(): NavState = state

    /** 发起“当前位置 -> 目的地”算路；目的地可为自动生成（前方 N 米） */
    fun calculateRoute(from: GeoPoint?) {
        val n = navi ?: return
        val start = from ?: lastOrigin
        if (start == null) {
            Log.w(TAG, "尚无定位，无法算路")
            return
        }
        val dLat: Double
        val dLon: Double
        if (destLat == 0.0 && destLon == 0.0 && autoDestMeters > 0) {
            dLat = start.lat + autoDestMeters / 111_320.0     // 北向偏移
            dLon = start.lon
            Log.i(TAG, "使用自动目的地：当前位置北向 ${autoDestMeters} 米")
        } else {
            dLat = destLat
            dLon = destLon
        }
        val fromPts = listOf(NaviLatLng(start.lat, start.lon))
        val toPts = listOf(NaviLatLng(dLat, dLon))
        val ok = n.calculateDriveRoute(fromPts, toPts, null, ROUTE_STRATEGY_DEFAULT)
        Log.i(TAG, "发起算路: ($start) -> ($dLat,$dLon) result=$ok")
    }

    // ---------------- 高德回调 -> NavState ----------------

    override fun onInitNaviSuccess() {
        Log.i(TAG, "onInitNaviSuccess：SDK 就绪（等待首个定位后自动算路）")
    }

    override fun onInitNaviFailure() {
        Log.e(TAG, "onInitNaviFailure：初始化失败（检查 Key / 包名 / SHA1 / 网络）")
    }

    override fun onCalculateRouteSuccess(result: AMapCalcRouteResult?) {
        Log.i(TAG, "算路成功，启动导航")
        try {
            navi?.startNavi(NAVI_MODE_GPS)
        } catch (e: Exception) {
            Log.e(TAG, "startNavi 失败：${e.message}")
        }
    }

    override fun onCalculateRouteFailure(errorCode: Int) {
        Log.e(TAG, "算路失败 errorCode=$errorCode（Key/网络/起终点是否合法）")
    }

    override fun onLocationChange(loc: AMapNaviLocation?) {
        val l = loc ?: return
        val c = l.coord
        if (c != null) lastOrigin = GeoPoint(c.latitude, c.longitude)
        state = state.copy(
            headingDeg = l.bearing.toInt(),
            speedKmh = (l.speed * 3.6f).toInt()          // 高德 Location.speed 为 m/s
        )
        if (!routeRequested && c != null) {              // 首个定位到达 -> 自动算路
            routeRequested = true
            calculateRoute(lastOrigin)
        }
    }

    override fun onNaviInfoUpdate(info: NaviInfo?) {
        val i = info ?: return
        state = state.copy(
            turnType = mapIcon(i.iconType),
            turnDistMeters = i.curStepRetainDistance,
            remainDistMeters = i.pathRetainDistance,
            totalDistMeters = if (state.totalDistMeters > 0) state.totalDistMeters
            else i.pathRetainDistance,
            currentRoad = i.currentRoadName ?: "",
            nextRoad = i.nextRoadName ?: "",
            speedKmh = if (i.currentSpeed > 0) i.currentSpeed else state.speedKmh,   // NaviInfo 速度单位已是 km/h
            elapsedSec = state.elapsedSec + 1,
            etaText = state.etaText
        )
    }

    override fun onArriveDestination() {
        Log.i(TAG, "到达目的地")
        state = state.copy(turnType = TurnType.ARRIVE, turnDistMeters = 0, remainDistMeters = 0)
    }

    override fun onEndEmulatorNavi() {
        Log.i(TAG, "模拟导航结束")
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
