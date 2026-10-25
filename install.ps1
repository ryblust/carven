#requires -Version 5.1
<#
.SYNOPSIS
Install a precompiled Carven toolchain from GitHub Releases.
.EXAMPLE
.\install.ps1 -Version 0.1.0
.EXAMPLE
.\install.ps1 -Prefix C:\Tools\Carven
#>
[CmdletBinding()]
param(
    [string]$Version,
    [string]$Prefix = (Join-Path $env:LOCALAPPDATA 'Programs\Carven')
)

$ErrorActionPreference = 'Stop'
$work = $null
$published = $false

try {
    if ($Prefix -notmatch '^(?:[A-Za-z]:[\\/]|\\\\[^\\/]+[\\/][^\\/]+(?:[\\/]|$))') {
        throw '-Prefix must be absolute'
    }
    $Prefix = [IO.Path]::GetFullPath($Prefix).TrimEnd([char[]]'\/')
    if ($Prefix -eq [IO.Path]::GetPathRoot($Prefix).TrimEnd([char[]]'\/')) {
        throw 'Cannot install at a filesystem root'
    }
    if ((Test-Path -LiteralPath $Prefix) -and
        -not (Test-Path -LiteralPath (Join-Path $Prefix '.carven-toolchain') -PathType Leaf)) {
        throw "Refusing to replace an unowned directory: $Prefix"
    }
    $parent = Split-Path -Parent $Prefix
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $work = Join-Path $parent ('.carven-install.' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $work | Out-Null

    $architecture = switch ([Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()) {
        'X64' { 'x64' }
        'Arm64' { 'arm64' }
        default { throw 'Unsupported Windows architecture' }
    }
    $origin = 'https://github.com/ryblust/carven/releases'
    $release = "$origin/latest/download"
    if ($Version) {
        if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Expected a numeric release version' }
        $release = "$origin/download/v$Version"
    }
    $name = "carven-win32-$architecture.tar.gz"
    $archive = Join-Path $work $name
    Invoke-WebRequest -UseBasicParsing -Uri "$release/$name" -OutFile $archive
    Invoke-WebRequest -UseBasicParsing -Uri "$release/$name.sha256" -OutFile "$archive.sha256"

    $expected = ((Get-Content -LiteralPath "$archive.sha256" -TotalCount 1) -split '\s+')[0]
    $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if ($expected -notmatch '^[0-9a-fA-F]{64}$' -or $expected -ne $actual) {
        throw 'Archive checksum mismatch'
    }

    $tar = Join-Path $env:SystemRoot 'System32\tar.exe'
    $staged = Join-Path $work 'toolchain'
    New-Item -ItemType Directory -Path $staged | Out-Null
    # Windows tar narrows path arguments; use relative names inside the working directory.
    Push-Location -LiteralPath $work
    try {
        $entries = & $tar -tzf $name
        if ($LASTEXITCODE -ne 0) { throw 'Cannot read toolchain archive' }
        foreach ($entry in $entries) {
            if ($entry -match '^[\\/]|^[A-Za-z]:|(^|[\\/])\.\.([\\/]|$)') {
                throw "Unsafe archive path: $entry"
            }
        }
        & $tar -xzf $name -C toolchain
        if ($LASTEXITCODE -ne 0) { throw 'Cannot extract toolchain archive' }
    } finally {
        Pop-Location
    }
    if (-not (Test-Path -LiteralPath (Join-Path $staged '.carven-toolchain') -PathType Leaf)) {
        throw 'Not a Carven toolchain archive'
    }
    & (Join-Path $staged 'bin\carven.exe') --version
    if ($LASTEXITCODE -ne 0) { throw 'The packaged compiler could not start' }

    $previous = Join-Path $work 'previous'
    if (Test-Path -LiteralPath $Prefix) { Move-Item -LiteralPath $Prefix -Destination $previous }
    try {
        Move-Item -LiteralPath $staged -Destination $Prefix
        $published = $true
    } catch {
        if (Test-Path -LiteralPath $previous) { Move-Item -LiteralPath $previous -Destination $Prefix }
        throw
    }
    Write-Output "Installed Carven in $Prefix"
    Write-Output "Add $Prefix\bin to your user PATH, then install the separate carven-lang.carven-vscode VS Code extension."
} catch {
    [Console]::Error.WriteLine("carven: $($_.Exception.Message)")
    exit 1
} finally {
    if ($work) {
        $previous = Join-Path $work 'previous'
        if (-not $published -and (Test-Path -LiteralPath $previous)) {
            [Console]::Error.WriteLine("Previous toolchain retained at $previous")
        } else {
            Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}
