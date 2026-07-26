$ErrorActionPreference = "Stop"

$wrapper = Join-Path $PSScriptRoot "tools/xmake-clang-module-pipeline/xmake.ps1"
& $wrapper @args
exit $LASTEXITCODE
