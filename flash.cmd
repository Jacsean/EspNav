@echo off
rem =====================================================================
rem  Flash firmware to the board and open the serial monitor.
rem  Double-click (default COM3) or drag a port arg: flash.cmd COM5
rem  It also rebuilds first if sources changed (idf.py flash depends on build).
rem  Exit the monitor with Ctrl + ]
rem  NOTE: pure ASCII on purpose (cmd.exe code page safe)
rem =====================================================================
chcp 65001 >nul
setlocal
set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM3"

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

echo.
echo === FLASH + MONITOR on %PORT% ===
echo   (if "port is busy": close other monitor/serial windows first)
echo   (exit monitor with Ctrl + ])
echo.
pushd "%~dp0Esp32S3"
"%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" -p %PORT% flash monitor
popd
echo.
echo === done ===
pause
