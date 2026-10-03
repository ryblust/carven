$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false

$scriptDir = $PSScriptRoot
$patchFile = Join-Path $scriptDir "xmake-3.1.1.patch"
$baseXmake = (Get-Command xmake.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source

$sourceProfile = $env:XMAKE_PROFILE
try {
    $env:XMAKE_PROFILE = $null
    $sourceInfo = @(& $baseXmake lua -c 'print(os.programdir()); print(tostring(xmake.version()))')
    if ($LASTEXITCODE -ne 0 -or $sourceInfo.Count -ne 2 -or -not $sourceInfo[0] -or -not $sourceInfo[1]) {
        throw "Failed to query the installed Xmake program directory and version."
    }
}
finally {
    $env:XMAKE_PROFILE = $sourceProfile
}
$sourceProgramDir = $sourceInfo[0]
$sourceVersion = $sourceInfo[1]

$patchedPaths = @(
    "modules/private/action/build/object.lua"
    "modules/private/action/build/link_objects.lua"
    "rules/c++/modules/clang/builder.lua"
    "rules/c++/modules/clang/scanner.lua"
    "rules/c++/modules/builder.lua"
    "rules/c++/modules/scanner.lua"
    "rules/c++/modules/support.lua"
    "rules/c++/modules/xmake.lua"
)
$keyParts = @($sourceProgramDir, $sourceVersion)
foreach ($relativePath in $patchedPaths) {
    $file = Join-Path $sourceProgramDir $relativePath
    $keyParts += (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
}
$keyParts += (Get-FileHash -LiteralPath $patchFile -Algorithm SHA256).Hash
$sha256 = [System.Security.Cryptography.SHA256]::Create()
try {
    $keyBytes = [System.Text.Encoding]::UTF8.GetBytes(($keyParts -join "`n"))
    $sourceKey = ([System.BitConverter]::ToString($sha256.ComputeHash($keyBytes))).Replace("-", "").ToLowerInvariant()
}
finally {
    $sha256.Dispose()
}

$cacheParent = Join-Path ([System.IO.Path]::GetTempPath()) "carven-xmake-clang-module-pipeline"
$overlay = Join-Path $cacheParent $sourceKey
$overlayMain = Join-Path $overlay "core/_xmake_main.lua"
$staging = $null

try {
    if (-not (Test-Path -LiteralPath $overlayMain -PathType Leaf)) {
        [System.IO.Directory]::CreateDirectory($cacheParent) | Out-Null
        $staging = Join-Path $cacheParent (".overlay." + [System.Guid]::NewGuid().ToString("N"))
        [System.IO.Directory]::CreateDirectory($staging) | Out-Null

        & robocopy.exe $sourceProgramDir $staging /E /COPY:DAT /DCOPY:DAT /R:1 /W:1 /MT:16 /NFL /NDL /NJH /NJS /NP
        $robocopyStatus = $LASTEXITCODE
        if ($robocopyStatus -ge 8) {
            throw "Failed to copy the Xmake program directory (robocopy exit code $robocopyStatus)."
        }

        # Git checkout and Windows packages can independently use CRLF. Normalize
        # only the staged copies, keeping the installed Xmake files untouched.
        $utf8 = [System.Text.UTF8Encoding]::new($false)
        foreach ($relativePath in $patchedPaths) {
            $file = Join-Path $staging $relativePath
            $content = [System.IO.File]::ReadAllText($file).Replace("`r`n", "`n")
            [System.IO.File]::WriteAllText($file, $content, $utf8)
        }
        $stagedPatch = Join-Path $staging ".carven-module-pipeline.patch"
        $content = [System.IO.File]::ReadAllText($patchFile).Replace("`r`n", "`n")
        [System.IO.File]::WriteAllText($stagedPatch, $content, $utf8)

        $git = (Get-Command git.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
        & $git -C $staging apply --check $stagedPatch
        if ($LASTEXITCODE -ne 0) {
            throw "The module-pipeline patch does not match the installed Xmake program files: $sourceProgramDir. Run the same command with xmake to use the stock pipeline."
        }
        & $git -C $staging apply $stagedPatch
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to apply the module-pipeline patch."
        }

        Remove-Item -LiteralPath $stagedPatch

        try {
            [System.IO.Directory]::Move($staging, $overlay)
            $staging = $null
        }
        catch [System.IO.IOException] {
            if (-not (Test-Path -LiteralPath $overlayMain -PathType Leaf)) {
                throw
            }
        }
    }
}
finally {
    if ($staging -and (Test-Path -LiteralPath $staging -PathType Container)) {
        Remove-Item -LiteralPath $staging -Recurse -Force
    }
}

$previousProgramDir = $env:XMAKE_PROGRAM_DIR
try {
    $env:XMAKE_PROGRAM_DIR = $overlay
    & $baseXmake @args
    $commandStatus = $LASTEXITCODE
}
finally {
    $env:XMAKE_PROGRAM_DIR = $previousProgramDir
}
exit $commandStatus
