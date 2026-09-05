$ErrorActionPreference = "Stop"

$wrapper = Join-Path $PSScriptRoot "xmake/clang-module-pipeline/xmake.ps1"
& $wrapper @args
exit $LASTEXITCODE
