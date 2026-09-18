package com.espnav.app.data

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Environment
import java.io.File

/**
 * 独立配置文件：写到**公共下载目录**，因此卸载/重装 App 都不会被删除。
 *
 * 路径：`/sdcard/Download/EspNav/espnav_config.json`
 *
 * 为什么要权限：Android 10+ 的分区存储（scoped storage）不允许 App 随意读写公共目录，
 * 因此需要「所有文件访问」权限（`MANAGE_EXTERNAL_STORAGE`，API 30+ 由用户在系统设置授予；
 * API 29 及以下用 `WRITE_EXTERNAL_STORAGE` + `requestLegacyExternalStorage`）。
 */
object ConfigFile {

    private const val DIR_NAME = "EspNav"
    private const val FILE_NAME = "espnav_config.json"

    fun dir(): File = File(
        Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS),
        DIR_NAME
    )

    fun file(): File = File(dir(), FILE_NAME)

    /** 供界面显示的完整路径 */
    fun pathText(): String = runCatching { file().absolutePath }.getOrDefault("(不可用)")

    /** 是否有读写该文件的权限 */
    fun hasPermission(): Boolean = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
        runCatching { Environment.isExternalStorageManager() }.getOrDefault(false)
    } else {
        true          /* API 29 及以下：靠 manifest 里的 WRITE_EXTERNAL_STORAGE + legacy 存储 */
    }

    fun exists(): Boolean = runCatching { file().isFile }.getOrDefault(false)

    fun save(json: String): Boolean = runCatching {
        val d = dir()
        if (!d.exists() && !d.mkdirs()) return false
        file().writeText(json, Charsets.UTF_8)
        true
    }.getOrDefault(false)

    fun load(): String? = runCatching {
        val f = file()
        if (f.isFile) f.readText(Charsets.UTF_8) else null
    }.getOrNull()

    fun delete(): Boolean = runCatching {
        val f = file()
        !f.exists() || f.delete()
    }.getOrDefault(false)

    /** 跳系统设置授予「所有文件访问」；API 30 以下返回 null（无需该权限） */
    fun permissionIntent(ctx: Context): Intent? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
        Intent(
            android.provider.Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
            Uri.parse("package:" + ctx.packageName)
        )
    } else {
        null
    }
}
