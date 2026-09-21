@echo off
rem =====================================================================
rem  Install out\espnav-debug.apk onto the phone over USB (adb).
rem  Requires: phone connected by USB + "USB debugging" enabled.
rem  If adb is unavailable or the phone is not connected, just copy
rem  out\espnav-debug.apk to the phone and install it manually (the APK
rem  lives in the same folder as this script).
rem  NOTE: pure ASCII on purpose (cmd.exe code page safe)
rem =====================================================================
chcp 65001 >nul
setlocal
set "OUT=%~dp0out"
set "ANDROID_HOME=C:\Users\jwgbo\AppData\Local\Android\Sdk"
set "ADB=%ANDROID_HOME%\platform-tools\adb.exe"
if not exist "%ADB%" set "ADB=adb"

if not exist "%OUT%\espnav-debug.apk" (
  echo.
  echo === out\espnav-debug.apk NOT FOUND - run build_all.cmd first ===
  pause
  exit /b 1
)

echo.
echo === installing %OUT%\espnav-debug.apk ===
"%ADB%" devices
"%ADB%" install -r "%OUT%\espnav-debug.apk"
if errorlevel 1 (
  echo.
  echo === adb install failed ===
  echo   - phone connected with USB debugging on?  check the list above
  echo   - otherwise: copy out\espnav-debug.apk to the phone and tap it
) else (
  echo.
  echo === installed. If the screen looks stale, uninstall the old app first. ===
)
pause
