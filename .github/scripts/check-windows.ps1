# Checks a Windows release layout (see package-windows.ps1) with dumpbin.
# Needs a Visual Studio developer prompt (dumpbin, and cl for -Smoke).
#   -Smoke  also compiles examples\simple.c and simple.cpp against the layout in
#           the three ways a consumer can link it and runs each one (not for
#           cross-built arm64).
param(
    [Parameter(Mandatory)][string]$Dir,
    [Parameter(Mandatory)][ValidateSet('x64', 'x86', 'arm64')][string]$Arch,
    [switch]$Smoke
)
$ErrorActionPreference = 'Stop'
$Dir = (Resolve-Path $Dir).Path
$examples = (Resolve-Path (Join-Path $PSScriptRoot '..\..\examples')).Path
$failures = New-Object System.Collections.Generic.List[string]

function Fail([string]$msg) { $failures.Add($msg); Write-Host "FAIL: $msg" }

function Invoke-Dumpbin([string[]]$a) {
    $out = & dumpbin.exe /nologo @a
    if ($LASTEXITCODE -ne 0) { throw "dumpbin $a failed ($LASTEXITCODE)" }
    $out
}

# --- latte.dll: imports --------------------------------------------------------
# Only Windows system DLLs. In particular no VCRUNTIME*, MSVCP*, ucrtbase*,
# api-ms-win-crt-*: the DLL must carry its own C runtime.
$allowedDlls = @('kernel32.dll', 'advapi32.dll', 'winhttp.dll')
$dll = Join-Path $Dir 'bin\latte.dll'
$deps = Invoke-Dumpbin @('/dependents', $dll) | ForEach-Object { if ($_ -match '^\s+(\S+\.dll)\s*$') { $Matches[1] } }
if (-not $deps) { Fail 'no dependents parsed from latte.dll' }
foreach ($d in $deps) {
    if ($allowedDlls -notcontains $d.ToLowerInvariant()) { Fail "latte.dll imports $d" }
}

# --- latte.dll: exports --------------------------------------------------------
# Rows look like: "ordinal hint RVA name"; unnamed (ordinal-only) rows are skipped.
$exports = Invoke-Dumpbin @('/exports', $dll) |
    ForEach-Object { if ($_ -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]{8}\s+(\S+)') { $Matches[1] } }
if (-not $exports) { Fail 'no exports parsed from latte.dll' }
foreach ($e in $exports) {
    if ($e -notlike 'latte_*') { Fail "latte.dll exports $e" }
}

# --- static libs: defined external symbols ---------------------------------------
# MSVC adds externals the code never declared. They are not API and cannot clash
# in practice, so they are ignored:
#   ??_C@...        string literal pool entries
#   __real@/__xmm@/__ymm@/__zmm@   floating point and SIMD constants
#   __pobj...       compiler-generated object data
#   .xxx            section-like names such as .CRT$XCU
$ignore = '^\?\?_C@', '^_{1,2}(real|xmm|ymm|zmm)@', '^_{0,2}pobj', '^\.'
# The UCRT stdio headers define these as inline functions; MSVC emits a COMDAT
# "pick any" copy into every object that calls one, so they cannot clash either.
$crtInline = '__local_stdio_printf_options', '__local_stdio_scanf_options',
    'printf', 'fprintf', 'sprintf', 'snprintf', '_snprintf', 'vsnprintf', 'vsprintf', 'sscanf', 'fscanf',
    '_vfprintf_l', '_vsnprintf_l', '_vsprintf_l', '_vsnprintf', '_vsscanf_l', '_vfscanf_l'
foreach ($lib in 'latte_mt.lib', 'latte_md.lib') {
    $path = Join-Path $Dir "lib\static\$lib"
    $count = 0
    Invoke-Dumpbin @('/symbols', $path) | ForEach-Object {
        # "idx offset SECTn|UNDEF|ABS type [()] External | name"
        if ($_ -notmatch '^[0-9A-Fa-f]+\s+[0-9A-Fa-f]{8}\s+(\S+)\s+\S+(?:\s+\(\))?\s+External\s+\|\s+(\S+)') { return }
        if ($Matches[1] -eq 'UNDEF') { return }
        $name = $Matches[2]
        foreach ($p in $ignore) { if ($name -match $p) { return } }
        # x86 cdecl decorates every C symbol with a leading underscore.
        if ($Arch -eq 'x86') { $name = $name -replace '^_', '' }
        if ($crtInline -contains $name) { return }
        $count++
        if ($name -notlike 'latte_*') { Fail "$lib defines $name" }
    }
    if ($count -eq 0) { Fail "no defined symbols parsed from $lib" }
}

# --- consumer smoke test ---------------------------------------------------------
# An empty key is rejected locally by latte_activate: no network involved.
function Invoke-Smoke([string]$label, [string]$src, [string[]]$clArgs, [string]$work) {
    $exe = Join-Path $work ($label + '.exe')
    $std = if ($src -like '*.cpp') { @('/EHsc', '/std:c++17') } else { @() }
    Push-Location $work
    try {
        $log = & cl /nologo @std "/I$Dir\include" $src "/Fe:$exe" "/Fo:$work\$label.obj" @clArgs /link /nologo
        if ($LASTEXITCODE -ne 0) { Fail "$label : cl failed`n$($log -join "`n")"; return }
    } finally { Pop-Location }
    # cmd merges stderr (where the example prints the error) without PowerShell turning it into errors.
    $out = (cmd /c "echo. | `"$exe`" 2>&1") -join "`n"
    if ($out -notmatch 'Activation error') { Fail "$label : output lacks 'Activation error': $out" }
    else { Write-Host "smoke ok: $label" }
}

if ($Smoke) {
    $sys = @('winhttp.lib', 'advapi32.lib')
    $work = Join-Path $Dir '..\smoke'
    New-Item -ItemType Directory -Force $work | Out-Null
    $work = (Resolve-Path $work).Path
    Copy-Item (Join-Path $Dir 'bin\latte.dll') $work -Force
    foreach ($lang in 'c', 'cpp') {
        $src = Join-Path $examples "simple.$lang"
        Invoke-Smoke "dyn_$lang"    $src @((Join-Path $Dir 'lib\latte.lib')) $work
        Invoke-Smoke "static_mt_$lang" $src (@('/MT', (Join-Path $Dir 'lib\static\latte_mt.lib')) + $sys) $work
        Invoke-Smoke "static_md_$lang" $src (@('/MD', (Join-Path $Dir 'lib\static\latte_md.lib')) + $sys) $work
    }
}

if ($failures.Count -gt 0) { throw "$($failures.Count) check(s) failed" }
Write-Host "windows checks passed ($Arch)"
