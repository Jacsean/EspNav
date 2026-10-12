package com.espnav.app.data

/**
 * 【M23】导航语音播报策略：把每帧的 [NavState] 变成"该不该播、播什么"。
 *
 * 参数（用户确认过）：
 *  · 转向：**单档 100 米** —— 进入 100 米内播一次「前方 100 米，右转」，不反复吵；
 *  · 距路口超过 150 米视为"已离开该路口"→ 复位，允许下一次再播（解决连续两个右转）；
 *  · 文案**用阿拉伯数字、不带路名**（Android 中文 TTS 会把"100 米"读成"一百米"，听感自然）；
 *  · 另外播报：到达终点、偏航重新规划、起步概览。
 *
 * 本类只做"决策"，不碰 TTS 与网络。
 */
class VoiceAnnouncer {

    companion object {
        /** 【M26】转向播报距离的默认值（米）。实际值由 App 设置驱动（见 [turnAnnounceM]）。 */
        const val TURN_ANNOUNCE_M_DEF = 100
        /** 复位阈值相对播报距离的余量：距离 + 该值 = "已离开此路口"的门限 */
        const val TURN_RESET_GAP_M = 50
    }

    /** 【M26】转向播报距离（米，可配 30-300）：进入该范围内播"前方 X 米，右转"。
     *  由 MainActivity 在每次调用前从设置同步过来（改完立即生效，无需重连/重启）。 */
    var turnAnnounceM: Int = TURN_ANNOUNCE_M_DEF

    /** 复位阈值：必须比播报距离大，否则"离开路口"永远判定不到 → 连续同类转向只播一次。
     *  所以这里取 [turnAnnounceM] + 余量（固定差值，避免用户把它调得比播报距离还小）。 */
    private val turnResetM: Int get() = turnAnnounceM + TURN_RESET_GAP_M

    private var announcedTurn: TurnType? = null   /* 本次路口是否已播过（按转向类型去重） */
    private var arrivedDone = false               /* 到达只播一次 */
    private var baseRecalc = -1                   /* 起步时的重算计数基线 */

    /** 开始导航：复位内部状态，并返回"起步概览"话术 */
    fun onStart(s: NavState): String {
        announcedTurn = null
        arrivedDone = false
        baseRecalc = s.recalcCount
        val d = s.totalDistMeters
        return when {
            d >= 1000 -> "开始导航，全程 " + fmtKm(d) + " 公里"
            d > 0     -> "开始导航，全程 " + d + " 米"
            else      -> "开始导航"
        }
    }

    /** 每帧调用；返回要播的文本，无需播报时返回 null */
    fun onNavState(s: NavState): String? {
        /* ---- 到达终点（只播一次）---- */
        if (s.turnType == TurnType.ARRIVE) {
            if (!arrivedDone) {
                arrivedDone = true
                return "已到达目的地附近，导航结束"
            }
            return null
        }

        /* ---- 偏航重新规划：高德重算成功 → recalcCount 增加 ---- */
        if (baseRecalc >= 0 && s.recalcCount > baseRecalc) {
            baseRecalc = s.recalcCount
            announcedTurn = null                  /* 新路线，允许重新播转向 */
            return "已偏离路线，正在重新规划"
        }

        /* ---- 转向：单档 100 米，同一路口只播一次 ---- */
        val d = s.turnDistMeters
        if (d > turnResetM) {
            announcedTurn = null                  /* 已离开上一个路口 */
            return null
        }
        if (d > turnAnnounceM || d <= 0) return null
        if (s.turnType == TurnType.STRAIGHT || s.turnType == TurnType.UNKNOWN) return null
        if (announcedTurn == s.turnType) return null
        announcedTurn = s.turnType
        return "前方 " + d + " 米，" + actionWord(s)
    }

    /** 转向动作的口语说法（与屏幕文案一致，但不念路名） */
    private fun actionWord(s: NavState): String = when (s.turnType) {
        TurnType.STRAIGHT -> "直行"
        TurnType.SLIGHT_LEFT -> "靠左行驶"
        TurnType.LEFT, TurnType.SHARP_LEFT -> "左转"
        TurnType.SLIGHT_RIGHT -> "靠右行驶"
        TurnType.RIGHT, TurnType.SHARP_RIGHT -> "右转"
        TurnType.UTURN -> "掉头"
        TurnType.MERGE -> "并线"
        TurnType.FORK_LEFT -> "靠左驶出"
        TurnType.FORK_RIGHT -> "靠右驶出"
        TurnType.ROUNDABOUT ->
            if (s.exitNumber > 0) "从第" + s.exitNumber + " 个出口驶出" else "驶出环岛"
        TurnType.ARRIVE -> "到达终点"
        TurnType.UNKNOWN -> "继续直行"
    }

    private fun fmtKm(m: Int): String {
        val t = String.format(java.util.Locale.US, "%.1f", m / 1000.0)
        return if (t.endsWith(".0")) t.substring(0, t.length - 2) else t
    }
}
