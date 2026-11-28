<#
.SYNOPSIS
Builds the MuRet client on Windows with MSVC, Ninja and vcpkg, and can package it as a zip.

.DESCRIPTION
Finds Visual Studio 2022 or newer with the C++ x64 tools and enters its developer shell, keeps a vcpkg clone at the
builtin-baseline of vcpkg.json (as CI does), then configures and builds with the windows-x64-ninja-* presets.
Running it again builds only what changed.

.PARAMETER Config
Release (the default) or Debug.

.PARAMETER Tests
Builds every target and runs the tests with CTest. Without it, only main.exe is built.

.PARAMETER Package
Copies main.exe, its DLLs, a muret-client.ini and the Data folder to build\package\<name>\ and zips it.

.PARAMETER NoData
With -Package, leaves Client\assets\Data out: main.exe then looks for a Data folder beside it.

.PARAMETER Clean
Deletes the preset's build folder first.

.PARAMETER Jobs
How many files to compile at once. By default Ninja's choice; lower it if cl runs out of memory (C1060).

.PARAMETER Define
CMake cache variables for the configure step, for example MU_XOR_REMAP=OFF.

.PARAMETER VcpkgRoot
A vcpkg clone to use as it is. By default the script keeps its own clone in %LOCALAPPDATA%\MuRet\vcpkg.

.EXAMPLE
Client\tools\windows\build.cmd

.EXAMPLE
Client\tools\windows\build.cmd -Package

.EXAMPLE
Client\tools\windows\build.cmd -Config Debug -Tests
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release',
    [switch]$Tests,
    [switch]$Package,
    [switch]$NoData,
    [switch]$Clean,
    [int]$Jobs = 0,
    [string[]]$Define = @(),
    [string]$VcpkgRoot = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$Preset = if ($Config -eq 'Debug') { 'windows-x64-ninja-debug' } else { 'windows-x64-ninja-release' }
$BuildDir = Join-Path $Repo "build\$Preset"
$BinDir = Join-Path $BuildDir 'bin'
$RepoData = Join-Path $Repo 'Client\assets\Data'
$FontFile = Join-Path $env:WINDIR 'Fonts\arial.ttf'

function Write-Step([string]$Text) {
    Write-Host "== $Text" -ForegroundColor Cyan
}

# Windows PowerShell turns a native command's stderr into errors when $ErrorActionPreference is Stop.
function Invoke-Native([string]$Command, [string[]]$Arguments) {
    Write-Host "> $Command $($Arguments -join ' ')" -ForegroundColor DarkGray
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Command @Arguments | Out-Host
    } finally {
        $ErrorActionPreference = $previous
    }
    if ($LASTEXITCODE -ne 0) {
        throw "$Command failed with exit code $LASTEXITCODE"
    }
}

function Test-Native([string]$Command, [string[]]$Arguments) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Command @Arguments 2>&1 | Out-Null
    } finally {
        $ErrorActionPreference = $previous
    }
    return $LASTEXITCODE -eq 0
}

function Enter-MsvcShell {
    if ((Get-Command cl.exe -ErrorAction SilentlyContinue) -and $env:VSCMD_ARG_TGT_ARCH -eq 'x64') {
        return
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw 'Visual Studio 2022 or newer is not installed (there is no vswhere.exe)'
    }
    $vs = & $vswhere -latest -products * -version '[17.0,)' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) {
        throw 'No Visual Studio 2022 or newer has the MSVC x64 tools: add "Desktop development with C++" in the Visual Studio Installer'
    }
    Write-Step "Visual Studio developer shell ($vs)"
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

function Assert-Tools {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        throw 'git is not on the PATH: install Git for Windows'
    }
    foreach ($tool in 'cmake', 'ninja') {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
            throw "$tool is not on the PATH: add ""C++ CMake tools for Windows"" in the Visual Studio Installer"
        }
    }
}

# The baseline pins the port versions, and the clone's checkout the scripts that build them: CI checks out the
# baseline too, so both build the same thing.
function Initialize-Vcpkg {
    $baseline = (Get-Content -Raw (Join-Path $Repo 'vcpkg.json') | ConvertFrom-Json).'builtin-baseline'
    if ($VcpkgRoot) {
        $root = (Resolve-Path $VcpkgRoot).Path
        if (-not (Test-Path (Join-Path $root 'scripts\buildsystems\vcpkg.cmake'))) {
            throw "$root is not a vcpkg clone"
        }
        if (-not (Test-Native git @('-C', $root, 'cat-file', '-e', "$baseline^{commit}"))) {
            throw "The vcpkg clone in $root lacks the baseline $baseline of vcpkg.json: update it with git -C ""$root"" pull"
        }
    } else {
        $root = Join-Path $env:LOCALAPPDATA 'MuRet\vcpkg'
        if (-not (Test-Path (Join-Path $root '.git'))) {
            Write-Step "vcpkg clone in $root"
            New-Item -ItemType Directory -Force (Split-Path $root) | Out-Null
            Invoke-Native git @('clone', 'https://github.com/microsoft/vcpkg', $root)
        }
        $head = (& git -C $root rev-parse HEAD).Trim()
        if ($head -ne $baseline) {
            Write-Step "vcpkg at the baseline $baseline"
            if (-not (Test-Native git @('-C', $root, 'cat-file', '-e', "$baseline^{commit}"))) {
                Invoke-Native git @('-C', $root, 'fetch', 'origin')
            }
            Invoke-Native git @('-C', $root, '-c', 'advice.detachedHead=false', 'checkout', '--quiet', $baseline)
            Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $root 'vcpkg.exe')
        }
    }
    if (-not (Test-Path (Join-Path $root 'vcpkg.exe'))) {
        Write-Step 'vcpkg bootstrap'
        Invoke-Native (Join-Path $root 'bootstrap-vcpkg.bat') @('-disableMetrics')
    }
    return $root
}

function Write-ClientIni([string]$Path, [string]$DataPath) {
    $lines = @(
        '; The Text.bmd of this Data names no font face, so the font goes here.',
        '[Data]',
        "Path = ""$DataPath""",
        '',
        '[Video]',
        "FontFile = ""$FontFile""",
        '',
        '; [Server]',
        '; Host = 127.0.0.1',
        '; Port = 44405'
    )
    [System.IO.File]::WriteAllLines($Path, [string[]]$lines, (New-Object System.Text.UTF8Encoding $false))
}

function Get-PackageName {
    $cmakeLists = Get-Content -Raw (Join-Path $Repo 'CMakeLists.txt')
    $version = [regex]::Match($cmakeLists, 'project\(MuRet\s+VERSION\s+([0-9.]+)').Groups[1].Value
    $name = "muret-client-$version"
    $clean = Test-Native git @('-C', $Repo, 'diff', '--quiet', 'HEAD')
    $tagged = Test-Native git @('-C', $Repo, 'describe', '--exact-match', '--tags', '--match', "v$version", 'HEAD')
    if (-not ($clean -and $tagged)) {
        $name += '-' + (& git -C $Repo rev-parse --short=8 HEAD).Trim()
        if (-not $clean) {
            $name += '-dirty'
        }
    }
    $name += '-windows-x64'
    if ($Config -eq 'Debug') {
        $name += '-debug'
    }
    if ($NoData) {
        $name += '-no-data'
    }
    return $name
}

function Get-DllImports([string]$File) {
    $lines = & dumpbin /nologo /dependents $File
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed on $File"
    }
    foreach ($line in $lines) {
        if ($line -match '^\s+(\S+\.dll)\s*$') {
            $Matches[1]
        }
    }
}

function Find-VcRuntime {
    if (-not $env:VCToolsRedistDir) {
        return $null
    }
    $crt = Get-ChildItem -Directory -ErrorAction SilentlyContinue (Join-Path $env:VCToolsRedistDir 'x64') |
        Where-Object { $_.Name -match '^Microsoft\.VC\d+\.CRT$' } | Select-Object -First 1
    if ($crt) {
        return $crt.FullName
    }
    return $null
}

# main.exe and every DLL it imports, transitively, from bin\ and, in Release, the Visual C++ runtime, so the zip runs
# on a PC without it (Debug's is not redistributable). Windows' own DLLs are in neither folder and stay out.
function Copy-WithImports([string]$Dir) {
    $folders = @($BinDir)
    if ($Config -eq 'Release') {
        $crt = Find-VcRuntime
        if ($crt) {
            $folders += $crt
        } else {
            Write-Warning 'The Visual C++ runtime DLLs were not found: the package needs the VC++ redistributable installed'
        }
    }
    $sources = @{}
    foreach ($folder in $folders) {
        foreach ($dll in Get-ChildItem $folder -Filter *.dll) {
            $key = $dll.Name.ToLowerInvariant()
            if (-not $sources.ContainsKey($key)) {
                $sources[$key] = $dll.FullName
            }
        }
    }
    $exe = Join-Path $BinDir 'main.exe'
    Copy-Item $exe $Dir
    $copied = @{}
    $queue = @($exe)
    while ($queue.Count -gt 0) {
        $next = @()
        foreach ($file in $queue) {
            foreach ($name in @(Get-DllImports $file)) {
                $key = $name.ToLowerInvariant()
                if ($sources.ContainsKey($key) -and -not $copied.ContainsKey($key)) {
                    $copied[$key] = $true
                    Copy-Item $sources[$key] $Dir
                    $next += $sources[$key]
                }
            }
        }
        $queue = $next
    }
}

function New-Package {
    $name = Get-PackageName
    $packageRoot = Join-Path $Repo 'build\package'
    $dir = Join-Path $packageRoot $name
    $zip = "$dir.zip"
    Write-Step "Package $name"
    foreach ($old in $dir, $zip) {
        if (Test-Path $old) {
            Remove-Item -Recurse -Force $old
        }
    }
    New-Item -ItemType Directory -Force $dir | Out-Null

    Copy-WithImports $dir
    if (-not $NoData) {
        Write-Host 'Copying Client\assets\Data...'
        Copy-Item -Recurse $RepoData (Join-Path $dir 'Data')
    }
    Write-ClientIni (Join-Path $dir 'muret-client.ini') 'Data'

    $version = & (Join-Path $dir 'main.exe') --version
    if ($LASTEXITCODE -ne 0) {
        throw "main.exe --version failed in the package (exit code $LASTEXITCODE): a DLL may be missing"
    }
    Write-Host $version
    Get-ChildItem $dir -File | Format-Table -AutoSize Name, Length | Out-Host

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory($dir, $zip,
        [System.IO.Compression.CompressionLevel]::Optimal, $true)
    return $zip
}

if (-not (Test-Path (Join-Path $Repo 'CMakePresets.json'))) {
    throw "$Repo has no CMakePresets.json: the script must stay in Client\tools\windows"
}
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Step "Deleting $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

Enter-MsvcShell
Assert-Tools
# The developer shell points VCPKG_ROOT to Visual Studio's own vcpkg, which is not a clone and has no baseline.
$env:VCPKG_ROOT = Initialize-Vcpkg
$env:VCPKG_DISABLE_METRICS = '1'

$configureArgs = @('--preset', $Preset) + @($Define | ForEach-Object { "-D$_" })
$buildArgs = @('--build', '--preset', $Preset)
if (-not $Tests) {
    $buildArgs += @('--target', 'Main')
}
if ($Jobs -gt 0) {
    $buildArgs += @('--parallel', "$Jobs")
}

Push-Location $Repo
try {
    Write-Step "Configure ($Preset)"
    Invoke-Native cmake $configureArgs
    Write-Step 'Build'
    Invoke-Native cmake $buildArgs
    if ($Tests) {
        if (-not $env:MU_CLIENT_FONT_FILE -and (Test-Path $FontFile)) {
            $env:MU_CLIENT_FONT_FILE = $FontFile
        }
        Write-Step 'Tests'
        Invoke-Native ctest @('--preset', $Preset, '--parallel', "$env:NUMBER_OF_PROCESSORS")
    }
} finally {
    Pop-Location
}

$exe = Join-Path $BinDir 'main.exe'
$devIni = Join-Path $BuildDir 'muret-client.ini'
if (-not (Test-Path $devIni)) {
    Write-ClientIni $devIni $RepoData
}
Write-Step 'Done'
Write-Host "main.exe: $exe"
Write-Host "Run it with the repo's Data: & ""$exe"" --config ""$devIni"""
if ($Package) {
    $zip = New-Package
    Write-Host "Package: $zip"
}
