@echo off
rem =====================================================================
rem  Build debug APK and copy it to <repo>\release\apk\espnav-debug.apk
rem  Double-click this file. Requires JDK 17 (bundled JBR path below).
rem  NOTE: pure ASCII on purpose (cmd.exe code page safe)
rem =====================================================================
chcp 65001 >nul
set "JAVA_HOME=C:\Users\jwgbo\.jdks\jbr-17.0.14"
set "ANDROID_HOME=C:\Users\jwgbo\AppData\Local\Android\Sdk"
set "VIRTUAL_ENV="
set "PYTHONHOME="
set "OUT=%~dp0..\..\release\apk"
if not exist "%OUT%" mkdir "%OUT%"

cd /d "%~dp0"
echo.
echo === building debug APK ===
call gradlew.bat assembleDebug --console=plain
if errorlevel 1 goto fail

copy /y "app\build\outputs\apk\debug\app-debug.apk" "%OUT%\espnav-debug.apk" >nul
echo.
echo === OK ===
echo   %OUT%\espnav-debug.apk
goto end

:fail
echo.
echo === APK BUILD FAILED - copy the error text above and send it to the agent ===

:end
pause
