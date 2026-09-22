# Esp32S3/tools/build_fw.ps1
# ---------------------------------------------------------------------------
# 固件增量编译（开发者/agent 用）。
#
# 为什么不用 idf.py：idf.py 的 Python 包装在本机沙箱里会踩
#   PermissionError: [WinError 5]
# 而直接跑 ninja 没问题。本脚本补齐 IDF 环境变量后调用 ninja。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File Esp32S3\tools\build_fw.ps1
#   powershell -ExecutionPolicy Bypass -File Esp32S3\tools\build_fw.ps1 -Clean
#
# 产物：Esp32S3\build\espnav.bin（及 bootloader / partition-table）
# ---------------------------------------------------------------------------
param([switch]$Clean)

$IDF_PATH_DIR = 'E:\esp\v6.1\esp-idf'
$TOOLS        = 'C:\Users\jwgbo\.espressif'

$env:IDF_PATH            = $IDF_PATH_DIR
$env:IDF_TOOLS_PATH      = $TOOLS
$env:IDF_PYTHON_ENV_PATH = "$TOOLS\python_env\idf6.1_py3.10_env"
$env:ESP_ROM_ELF_DIR     = "$TOOLS\tools\esp-rom-elfs\20241011"
$env:ESP_IDF_VERSION     = '6.1.0'
$env:PATH = "$TOOLS\tools\ninja\1.12.1;$TOOLS\tools\cmake\4.0.3\bin;" +
            "$TOOLS\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin;$env:PATH"

$root  = ($PSScriptRoot | Split-Path -Parent)          # Esp32S3
$build = Join-Path $root 'build'

if ($Clean) {
    Write-Host "clean: removing $build"
    Remove-Item -Recurse -Force $build -ErrorAction SilentlyContinue
}
if (-not (Test-Path $build)) { New-Item -ItemType Directory $build | Out-Null }

Set-Location $build
ninja
exit $LASTEXITCODE
