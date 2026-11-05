# FFXIV-FontChanger 上游代码同步脚本
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir

# 设置本地代理
$env:HTTP_PROXY = "http://127.0.0.1:7897"
$env:HTTPS_PROXY = "http://127.0.0.1:7897"
$env:ALL_PROXY = "http://127.0.0.1:7897"

Set-Location $repoRoot
node scripts/update_fontchanger.ts
