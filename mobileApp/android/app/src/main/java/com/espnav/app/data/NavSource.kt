package com.espnav.app.data

/**
 * 导航数据源抽象：**模拟器**与**高德导航 SDK**都实现它，对外只产出统一的 [NavState]。
 * 这样 App/屏幕端完全不感知数据来自哪里（接高德时只替换实现，其余零改动）。
 *
 * [latest] 语义统一为“取当前应当显示的状态”：
 *  - MockNavSource：每调用一次前进一小步（用于联调演示）
 *  - AmapNavSource：返回导航回调缓存下来的最新状态（不推进）
 */
interface NavSource {
    /** 数据源显示名（界面用） */
    val displayName: String

    /** 开始产出（高德实现里会在此初始化 SDK / 起导航） */
    fun start()

    /** 停止（高德实现里停止导航，释放资源） */
    fun stop()

    /** 取当前状态 */
    fun latest(): NavState
}

/** 模拟数据源：包装 [MockNavigator]，保持既有联调行为 */
class MockNavSource(private val totalDistMeters: Int = 8000) : NavSource {

    private val mock = MockNavigator(totalDistMeters)

    override val displayName: String get() = "模拟导航"

    override fun start() {
        mock.reset()
    }

    override fun stop() = Unit

    override fun latest(): NavState = mock.next()

    /** 当前阶段名（仅模拟源有；界面展示用） */
    fun stageName(): String = mock.stageName()
}
