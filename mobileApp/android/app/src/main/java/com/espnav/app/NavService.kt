package com.espnav.app

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import android.os.PowerManager

/**
 * 前台服务：让 App 在**熄屏/后台**时保持 TCP 连接。
 *
 * 背景：Android 会在屏幕熄灭后挂起普通前台 Activity 的网络活动，
 * 导致与 ESP32 的 TCP 断开（固件随后按协议 §6 清屏）。
 * 本服务持有常驻通知 + PARTIAL_WAKE_LOCK，使连接在熄屏时继续工作。
 */
class NavService : Service() {

    companion object {
        private const val CH_ID = "espnav_nav"
        private const val NOTI_ID = 1001

        fun start(ctx: Context) {
            val i = Intent(ctx, NavService::class.java)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) ctx.startForegroundService(i)
            else ctx.startService(i)
        }

        fun stop(ctx: Context) {
            runCatching { ctx.stopService(Intent(ctx, NavService::class.java)) }
        }
    }

    private var wake: PowerManager.WakeLock? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val nm = getSystemService(NOTIFICATION_SERVICE) as NotificationManager
            nm.createNotificationChannel(
                NotificationChannel(CH_ID, "导航连接", NotificationManager.IMPORTANCE_LOW)
            )
        }
        val builder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            Notification.Builder(this, CH_ID)
        } else {
            @Suppress("DEPRECATION")
            Notification.Builder(this)
        }
        val n: Notification = builder
            .setContentTitle("EspNav 导航屏已连接")
            .setContentText("保持与 ESP32 的连接（熄屏不会断开）")
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setOngoing(true)
            .build()
        startForeground(NOTI_ID, n)

        runCatching {
            val pm = getSystemService(POWER_SERVICE) as PowerManager
            wake = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "espnav:nav").apply { acquire() }
        }
    }

    override fun onDestroy() {
        runCatching {
            val w = wake
            if (w != null && w.isHeld) w.release()
        }
        wake = null
        super.onDestroy()
    }
}
