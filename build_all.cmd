@echo off
rem =====================================================================
rem  ONE-STOP BUILD: firmware + APK -> .\out\
rem  Double-click this file (it lives in the repo root on purpose).
rem  NOTE: this file is intentionally pure ASCII (cmd.exe code page safe).
rem
rem  Key behavior (2026-09-22, fixes the "installed a stale APK" problem):
rem   1) deletes old artifacts in out\ BEFORE building -> if the build fails,
rem      out\ contains NOTHING, so a stale APK can never be installed again;
rem   2) build logs go to build_fw.log / build_apk.log (full log, always);
rem   3) on failure only KEY error lines are printed (e: / error / FAILED);
rem   4) on success the artifact TIMESTAMPS are printed for verification.
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
echo === [1/2] building FIRMWARE  (log: build_fw.log) ===
pushd "%ROOT%Esp32S3"
"%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" build > "%ROOT%build_fw.log" 2>&1
if errorlevel 1 goto fail_fw
copy /y "build\espnav.bin" "%OUT%\espnav.bin" >nul
copy /y "build\bootloader\bootloader.bin" "%OUT%\bootloader.bin" >nul
copy /y "build\partition_table\partition-table.bin" "%OUT%\partition-table.bin" >nul
popd

rem ---------- 2/2 APK ----------
echo.
echo === [2/2] building APK       (log: build_apk.log) ===
set "JAVA_HOME=C:\Users\jwgbo\.jdks\jbr-17.0.14"
set "ANDROID_HOME=C:\Users\jwgbo\AppData\Local\Android\Sdk"
pushd "%ROOT%mobileApp\android"
call gradlew.bat assembleDebug --console=plain > "%ROOT%build_apk.log" 2>&1
if errorlevel 1 goto fail_apk
copy /y "app\build\outputs\apk\debug\app-debug.apk" "%OUT%\espnav-debug.apk" >nul
popd

echo.
echo ============================================================
echo  DONE - artifacts in out\ (timestamps below must be NOW):
echo ============================================================
dir "%OUT%\espnav.bin" "%OUT%\espnav-debug.apk"
echo.
echo  NEXT (your side):
echo    1) flash firmware : run inside Esp32S3\ : idf.py -p COM3 flash monitor
echo    2) install APK    : copy out\espnav-debug.apk to the phone and tap it
goto end

:fail_fw
popd
echo.
echo ############################################################
echo  ##  FIRMWARE BUILD FAILED - out\ has NO firmware (stale deleted)
echo  ##  full log : %ROOT%build_fw.log
echo  ##  key error lines:
echo ############################################################
findstr /C:"error:" /C:"FAILED" /C:"Error" "%ROOT%build_fw.log"
echo ############################################################
goto end

:fail_apk
popd
echo.
echo ############################################################
echo  ##  APK BUILD FAILED - out\ has NO apk (stale one deleted),
echo  ##  so a stale build can never be installed again.
echo  ##  full log : %ROOT%build_apk.log
echo  ##  key error lines (Kotlin errors start with "e: "):
echo ############################################################
findstr /C:"e: " /C:"error: " /C:"FAILURE:" /C:"Execution failed" "%ROOT%build_apk.log"
echo ############################################################
echo   paste those lines to the agent.
goto end

:end
pause
