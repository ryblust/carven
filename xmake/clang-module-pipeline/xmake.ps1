$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false

$scriptDir = $PSScriptRoot
$patchFile = Join-Path $scriptDir "xmake-3.1.1.patch"
$baseXmake = (Get-Command xmake.exe -CommandType Application -ErrorAction Stop).Source

$sourceProgramDir = (& $baseXmake lua -c 'io.write(os.programdir())' | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or -not $sourceProgramDir) {
    throw "Failed to query the installed Xmake program directory."
}
$sourceVersion = (& $baseXmake lua -c 'io.write(tostring(xmake.version()))' | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or -not $sourceVersion) {
    throw "Failed to query the installed Xmake version."
}

$patchedFiles = @(
    (Join-Path $sourceProgramDir "rules/c++/modules/clang/builder.lua")
    (Join-Path $sourceProgramDir "rules/c++/modules/builder.lua")
    (Join-Path $sourceProgramDir "rules/c++/modules/xmake.lua")
    $patchFile
)
$keyParts = @($sourceProgramDir, $sourceVersion)
foreach ($file in $patchedFiles) {
    $keyParts += (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
}
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

        $git = (Get-Command git.exe -CommandType Application -ErrorAction Stop).Source
        & $git -C $staging apply --check $patchFile
        if ($LASTEXITCODE -ne 0) {
            throw "The module-pipeline patch does not match the installed Xmake program files: $sourceProgramDir. Run the same command with xmake to use the stock pipeline."
        }
        & $git -C $staging apply $patchFile
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to apply the module-pipeline patch."
        }

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

$env:XMAKE_PROGRAM_DIR = $overlay
& $baseXmake @args
exit $LASTEXITCODE
