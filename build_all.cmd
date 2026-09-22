@echo off
rem =====================================================================
rem  ONE-STOP BUILD: firmware + APK -> .\out\
rem  Double-click this file (repo root). Pure ASCII on purpose.
rem
rem  Behavior:
rem   1) clears old artifacts in out\ BEFORE building -> a failed build
rem      leaves NOTHING in out\, so a stale APK can never be installed;
rem   2) build output is shown LIVE on screen (no redirect), so you can see
rem      progress instead of thinking it hung;
rem   3) on failure you get a short banner telling you what to copy back;
rem   4) on success the artifact timestamps are printed.
rem =====================================================================
chcp 65001 >nul
setlocal
set "ROOT=%~dp0"
set "OUT=%ROOT%out"
if not exist "%OUT%" mkdir "%OUT%"

echo.
echo === [0/2] clear old artifacts in out\ ===
del /q "%OUT%\espnav.bin" 2>nul
del /q "%OUT%\bootloader.bin" 2>nul
del /q "%OUT%\partition-table.bin" 2>nul
del /q "%OUT%\espnav-debug.apk" 2>nul

rem ---------- ESP-IDF environment ----------
set "MSYSTEM="
set "MSYSCON="
set "PYTHONUTF8=1"
set "VIRTUAL_ENV="
set "PYTHONHOME="
set "IDF_PATH=E:\esp\v6.1\esp-idf"
set "IDF_TOOLS_PATH=C:\Users\jwgbo\.espressif"
set "IDF_PYTHON_ENV_PATH=%IDF_TOOLS_PATH%\python_env\idf6.1_py3.10_env"
set "ESP_ROM_ELF_DIR=%IDF_TOOLS_PATH%\tools\esp-rom-elfs\20241011"
set "ESP_IDF_VERSION=6.1.0"
set "PATH=%IDF_TOOLS_PATH%\tools\ninja\1.12.1;%IDF_TOOLS_PATH%\tools\cmake\4.0.3\bin;%IDF_TOOLS_PATH%\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin;%PATH%"

rem ---------- 1/2 firmware ----------
echo.
echo === [1/2] building FIRMWARE   (about 1-3 min, output below) ===
pushd "%ROOT%Esp32S3"
"%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" build
if errorlevel 1 goto fail_fw
copy /y "build\espnav.bin" "%OUT%\espnav.bin" >nul
copy /y "build\bootloader\bootloader.bin" "%OUT%\bootloader.bin" >nul
copy /y "build\partition_table\partition-table.bin" "%OUT%\partition-table.bin" >nul
popd

rem ---------- 2/2 APK ----------
echo.
echo === [2/2] building APK        (about 1-2 min, output below) ===
set "JAVA_HOME=C:\Users\jwgbo\.jdks\jbr-17.0.14"
set "ANDROID_HOME=C:\Users\jwgbo\AppData\Local\Android\Sdk"
pushd "%ROOT%mobileApp\android"
call gradlew.bat assembleDebug --console=plain
if errorlevel 1 goto fail_apk
copy /y "app\build\outputs\apk\debug\app-debug.apk" "%OUT%\espnav-debug.apk" >nul
popd

echo.
echo ============================================================
echo ##                                                        ##
echo ##   BUILD OK  -  FIRMWARE + APK  BOTH BUILT SUCCESSFULLY  ##
echo ##                                                        ##
echo ============================================================
echo  artifacts in out\ (timestamps below must be NOW):
dir "%OUT%\espnav.bin" "%OUT%\espnav-debug.apk"
echo.
echo  NEXT (your side):
echo    1) flash firmware : in Esp32S3\  run:  idf.py -p COM3 flash monitor
echo    2) install APK    : copy out\espnav-debug.apk to the phone and tap it
goto end

:fail_fw
popd
echo.
echo !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
echo !!!  BUILD FAILED : FIRMWARE  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
echo !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
echo  ##  FIRMWARE BUILD FAILED
echo  ##  out\ has NO firmware (old ones were deleted on purpose).
echo  ##  Scroll UP, copy the "error:" lines and send them to the agent.
echo ############################################################
goto end

:fail_apk
popd
echo.
echo !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
echo !!!  BUILD FAILED : APK       !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
echo !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
echo  ##  APK BUILD FAILED
echo  ##  out\ has NO apk (the old one was deleted on purpose),
echo  ##  so a stale build can never be installed again.
echo  ##  Scroll UP, copy the "e: " / "ERROR:" lines to the agent.
echo ############################################################
goto end

:end
pause
