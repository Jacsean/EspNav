@echo off
rem =====================================================================
rem  UPDATE THE APP ON THE PHONE   (just double-click this file)
rem
rem  Why "in-place upgrade" first:
rem    Android KEEPS app data (/data/data/<pkg>) when you install over the
rem    existing app with the same signature. So the settings are preserved
rem    WITHOUT any backup. Only if that fails (e.g. the signing key changed)
rem    do we back up and do a clean uninstall + install.
rem
rem  Settings safety net:
rem    The app itself also exports settings to
rem    /sdcard/Download/EspNav/espnav_config.json (a public dir that survives
rem    uninstall), and auto-restores them on first launch.
rem
rem  NOTE: this file is pure ASCII on purpose (cmd.exe code page safe).
rem =====================================================================
setlocal enabledelayedexpansion
set "ROOT=%~dp0"
set "APK=%ROOT%..\..\..\out\espnav-debug.apk"
set "PKG=com.espnav.app"
set "BAK=%ROOT%..\..\..\out\espnav_config.backup.json"
set "PREFS=%ROOT%prefs.backup.xml"

echo.
echo === [1/4] locate APK ===
if not exist "%APK%" (
    echo ############################################################
    echo ##  APK NOT FOUND
    echo ##    %APK%
    echo ##  Run build_all.cmd in the repo root first.
    echo ############################################################
    goto end
)
echo     %APK%

echo.
echo === [2/4] adb + device ===
set "ADB=adb"
where adb >nul 2>nul
if errorlevel 1 set "ADB=%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe"
if not exist "%ADB%" (
    if /i not "%ADB%"=="adb" (
        echo ############################################################
        echo ##  adb NOT FOUND (tried PATH and %%LOCALAPPDATA%%\Android\Sdk)
        echo ##  Fallback: copy out\espnav-debug.apk to the phone and tap it.
        echo ############################################################
        goto end
    )
)
"%ADB%" start-server >nul 2>nul
"%ADB%" devices | findstr /R /C:"device$" >nul
if errorlevel 1 (
    echo ############################################################
    echo ##  NO DEVICE FOUND
    echo ##  1) connect the phone by USB
    echo ##  2) enable Developer options -^> USB debugging
    echo ##  3) accept the "Allow USB debugging?" prompt on the phone
    echo ############################################################
    goto end
)
echo     device OK

echo.
echo === [3/4] already installed? ===
"%ADB%" shell pm list packages %PKG% | findstr /C:"%PKG%" >nul
if errorlevel 1 goto fresh

echo     yes - trying IN-PLACE upgrade first (settings are kept) ...
"%ADB%" install -r "%APK%"
if not errorlevel 1 goto done

echo.
echo ###  in-place upgrade FAILED (usually: signing key changed)  ###
echo ###  backing up settings, then uninstall + install          ###
echo.
echo --- 1) backup exported config (public dir) ---
"%ADB%" pull /sdcard/Download/EspNav/espnav_config.json "%BAK%" 2>nul
if exist "%BAK%" (echo     saved: %BAK%) else (echo     no exported config found - defaults will be used)
echo --- 2) backup shared_prefs via run-as (works on debug builds) ---
"%ADB%" shell run-as %PKG% cat shared_prefs/espnav_prefs.xml > "%PREFS%" 2>nul
if exist "%PREFS%" (echo     saved: %PREFS%) else (echo     run-as unavailable - skipped)
echo --- 3) uninstall ---
"%ADB%" uninstall %PKG%
if errorlevel 1 (
    echo.
    echo ###  UNINSTALL FAILED - stopped. Read the message above.  ###
    goto end
)

:fresh
echo.
echo === [4/4] install ===
"%ADB%" install "%APK%"
if errorlevel 1 (
    echo ############################################################
    echo ##  INSTALL FAILED - read the message above.
    echo ##  (common cause: "INSTALL_FAILED_UPDATE_INCOMPATIBLE" means a
    echo ##   different signature is on the phone; uninstall it manually.)
    echo ############################################################
    goto end
)

echo.
echo --- restore shared_prefs (only if we backed it up) ---
if not exist "%PREFS%" goto done
"%ADB%" push "%PREFS%" /data/local/tmp/espnav_prefs.xml >nul 2>nul
"%ADB%" shell run-as %PKG% mkdir -p shared_prefs >nul 2>nul
"%ADB%" shell run-as %PKG% cp /data/local/tmp/espnav_prefs.xml shared_prefs/espnav_prefs.xml >nul 2>nul
"%ADB%" shell rm -f /data/local/tmp/espnav_prefs.xml >nul 2>nul
del /q "%PREFS%" >nul 2>nul
echo     prefs restored (if run-as was available)

:done
echo.
echo ============================================================
echo  DONE - the app on the phone has been updated.
echo.
echo  Settings:
echo    * in-place upgrade      -> kept automatically
echo    * uninstall + install   -> restored above if possible;
echo      otherwise just launch the app once, it auto-restores from
echo      /sdcard/Download/EspNav/espnav_config.json
echo ============================================================
goto end

:end
pause
