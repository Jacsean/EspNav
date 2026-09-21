@echo off
rem =====================================================================
rem  ONE-STOP BUILD: firmware + APK, outputs collected into .\out\
rem  Double-click this file (it lives in the repo root on purpose, so you
rem  never have to switch folders).
rem
rem  Outputs:
rem    out\espnav.bin          firmware image (flash offset 0x10000)
rem    out\espnav-debug.apk    debug APK (copy to phone and install)
rem
rem  Next steps (also printed at the end):
rem    - flash firmware : double-click flash.cmd   (default COM3)
rem    - install APK    : copy out\espnav-debug.apk to the phone
rem
rem  NOTE: pure ASCII on purpose (cmd.exe code page safe)
rem =====================================================================
chcp 65001 >nul
setlocal
set "ROOT=%~dp0"
set "OUT=%ROOT%out"
if not exist "%OUT%" mkdir "%OUT%"

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
echo === [1/2] building FIRMWARE ===
pushd "%ROOT%Esp32S3"
"%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" build
if errorlevel 1 goto fail_fw
copy /y "build\espnav.bin" "%OUT%\espnav.bin" >nul
popd

rem ---------- 2/2 APK ----------
echo.
echo === [2/2] building APK ===
set "JAVA_HOME=C:\Users\jwgbo\.jdks\jbr-17.0.14"
set "ANDROID_HOME=C:\Users\jwgbo\AppData\Local\Android\Sdk"
pushd "%ROOT%mobileApp\android"
call gradlew.bat assembleDebug --console=plain
if errorlevel 1 goto fail_apk
copy /y "app\build\outputs\apk\debug\app-debug.apk" "%OUT%\espnav-debug.apk" >nul
popd

echo.
echo ============================================================
echo  DONE - both artifacts are in the SAME folder:
echo    %OUT%\espnav.bin          (firmware, offset 0x10000)
echo    %OUT%\espnav-debug.apk    (install on phone)
echo.
echo  NEXT:
echo    1) flash firmware : double-click flash.cmd   (default COM3)
echo    2) install APK    : copy out\espnav-debug.apk to the phone
echo       (or double-click install_apk.cmd if the phone is on USB)
echo ============================================================
goto end

:fail_fw
popd
echo.
echo === FIRMWARE BUILD FAILED - copy the error text above and send it to the agent ===
goto end

:fail_apk
popd
echo.
echo === APK BUILD FAILED - copy the error text above and send it to the agent ===

:end
pause
