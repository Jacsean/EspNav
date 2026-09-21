@echo off
rem ===================================================================
rem  一键烧录 + 看日志（双击即可；默认 COM3，可拖参数改端口：flash.cmd COM5）
rem  为什么不用 IDF 安装器的 "IDF PowerShell Environment"：
rem    本机 IDF 工具在 C:\Users\jwgbo\.espressif（不是 E:\Espressif），
rem    且 python_env 只存在于该目录 —— 直接调用可绕过 export 脚本的环境缺失。
rem  停止看日志：按 Ctrl+] （或在窗口里按任意键后回车）
rem ===================================================================
chcp 65001 >nul
set MSYSTEM=
set MSYSCON=
set PYTHONUTF8=1
set IDF_PATH=E:\esp\v6.1\esp-idf
set IDF_TOOLS_PATH=C:\Users\jwgbo\.espressif
set IDF_PYTHON_ENV_PATH=%IDF_TOOLS_PATH%\python_env\idf6.1_py3.10_env
set ESP_IDF_VERSION=6.1.0
set PATH=%IDF_TOOLS_PATH%\tools\ninja\1.12.1;%IDF_TOOLS_PATH%\tools\cmake\4.0.3\bin;%IDF_TOOLS_PATH%\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin;%PATH%

set PORT=%1
if "%PORT%"=="" set PORT=COM3

cd /d "%~dp0.."
echo.
echo === EspNav 固件烧录 ===
echo   串口：%PORT%      （不对就改用：flash.cmd COM5）
echo   退出日志：Ctrl + ]
echo.
"%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" -p %PORT% flash monitor
echo.
echo === 结束（若报串口占用：先关掉其它监视窗口）===
pause
