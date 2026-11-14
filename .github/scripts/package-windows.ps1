# Builds latte-c for one Windows architecture and packs it as
#   <Out>\latte-c-<Version>-windows-<Arch>.zip
# Needs a Visual Studio developer prompt for <Arch> (cmake, dumpbin, cl).
# x64 and x86 also run ctest and the consumer smoke test; arm64 is cross-built
# and never executed.
param(
    [Parameter(Mandatory)][ValidateSet('x64', 'x86', 'arm64')][string]$Arch,
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$Out
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$platform = @{ x64 = 'x64'; x86 = 'Win32'; arm64 = 'ARM64' }[$Arch]
$name = "latte-c-$Version-windows-$Arch"
$stage = Join-Path $Out 'stage'
$layout = Join-Path $Out $name
$runnable = $Arch -ne 'arm64'

function Run([string]$exe, [string[]]$a) {
    & $exe @a | Out-Host   # keep tool output out of the caller's return value
    if ($LASTEXITCODE -ne 0) { throw "$exe $($a -join ' ') failed ($LASTEXITCODE)" }
}

# Configure, build and install one tree into the shared staging prefix.
function Build([string]$tag, [string]$runtime, [string[]]$extra) {
    $bd = Join-Path $Out "build-$tag"
    $cfg = @('-S', $root, '-B', $bd, '-A', $platform, "-DCMAKE_MSVC_RUNTIME_LIBRARY=$runtime",
        "-DCMAKE_INSTALL_PREFIX=$stage") + $extra
    Run cmake $cfg
    Run cmake @('--build', $bd, '--config', 'Release', '--parallel')
    Run cmake @('--install', $bd, '--config', 'Release')
    $bd
}

# /MT: shared + static (latte.dll, latte.lib, latte_mt.lib).
$mt = Build 'mt' 'MultiThreaded' @()
if ($runnable) { Run ctest @('--test-dir', $mt, '-C', 'Release', '--output-on-failure') }
# /MD: static only (latte_md.lib).
$null = Build 'md' 'MultiThreadedDLL' @('-DLATTE_BUILD_SHARED=OFF')

if (Test-Path $layout) { Remove-Item -Recurse -Force $layout }
foreach ($d in 'include\latte', 'bin', 'lib\static', 'licenses') {
    New-Item -ItemType Directory -Force (Join-Path $layout $d) | Out-Null
}
Copy-Item "$stage\include\latte\latte.h*" "$layout\include\latte"
Copy-Item "$stage\bin\latte.dll" "$layout\bin"
Copy-Item "$stage\lib\latte.lib" "$layout\lib"
Copy-Item "$stage\lib\static\latte_mt.lib", "$stage\lib\static\latte_md.lib" "$layout\lib\static"
Copy-Item "$root\LICENSE" $layout
Copy-Item "$root\vendor\monocypher\LICENCE.md" "$layout\licenses\Monocypher.md"
Copy-Item "$root\vendor\cjson\LICENSE" "$layout\licenses\cJSON.txt"
(Get-Content -Raw "$root\.github\release\README.txt").Replace('@VERSION@', $Version).Replace('@ARCH@', $Arch) |
    Set-Content -NoNewline "$layout\README.txt"

$check = @('-Dir', $layout, '-Arch', $Arch)
if ($runnable) { $check += '-Smoke' }
Run pwsh (@('-NoProfile', '-File', "$PSScriptRoot\check-windows.ps1") + $check)

$zip = Join-Path $Out "$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $layout -DestinationPath $zip
Write-Host "created $zip"
