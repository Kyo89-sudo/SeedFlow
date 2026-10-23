[CmdletBinding()]
param([switch]$NoPause, [switch]$CheckOnly, [switch]$DebugBuild, [switch]$SharedLibraries, [switch]$RunTests)
# SeedFlow 0.8.7 - build nativa Win32 C++ con libtorrent statico.
# Requisiti: Visual Studio 2026 (18) o 2022 (17) con "Sviluppo desktop con C++",
# CMake (incluso in VS) e Git. NON serve piu' .NET.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$log = Join-Path $root 'COMPILAZIONE.log'
$stage = 'inizializzazione'
$started = $false
$triplet = if ($SharedLibraries) { 'x64-windows' } else { 'x64-windows-static' }
$config = if ($DebugBuild) { 'Debug' } else { 'Release' }
# VsDevCmd may set VCPKG_ROOT to the VS bundle (not a classic checkout): read it first.
$requestedVcpkgRoot = $env:VCPKG_ROOT

function Select-VcpkgRoot([string]$Requested, [string]$Fallback) {
    if (!$Requested) { return $Fallback }
    $candidate = [IO.Path]::GetFullPath($Requested)
    if (!(Test-Path -LiteralPath $candidate)) { return $candidate }
    if ((Test-Path -LiteralPath (Join-Path $candidate 'ports')) -and
        (Test-Path -LiteralPath (Join-Path $candidate 'scripts\buildsystems\vcpkg.cmake'))) { return $candidate }
    Write-Host "VCPKG_ROOT non adatto alla modalita' classic: $candidate - uso $Fallback"
    return $Fallback
}
function Run([string]$Exe, [string[]]$Arguments) {
    Write-Host ("> $Exe " + ($Arguments -join ' '))
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Exe @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previous }
    if ($code -ne 0) { throw "Comando fallito (codice ${code}): $Exe" }
}
function Find-Exe([string[]]$Candidates) {
    foreach ($c in $Candidates) {
        if ($c -and (Test-Path -LiteralPath $c -PathType Leaf)) { return (Get-Item -LiteralPath $c).FullName }
        $cmd = Get-Command $c -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($cmd) { return $cmd.Source }
    }
    return $null
}

try {
    if ($SharedLibraries -and $DebugBuild) { throw 'Il pacchetto shared e previsto per Release. Per il debug usa CMake e il runtime di sviluppo.' }
    Start-Transcript -LiteralPath $log -Force | Out-Null
    $started = $true
    Set-Location -LiteralPath $root
    Write-Host "SeedFlow 0.8.7 (Win32 nativa) - build $config $triplet - $(Get-Date -Format o)"
    Write-Host "Sorgenti: $root"

    $stage = 'Visual Studio'
    $vswhere = Find-Exe @("${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe", 'vswhere.exe')
    if (!$vswhere) { throw 'vswhere.exe non trovato: installa o ripara Visual Studio Installer.' }
    $vs = & $vswhere -latest -products '*' -version '[17.0,19.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or !$vs) { throw 'Installa Visual Studio 2026 o 2022 con "Sviluppo desktop con C++" (MSVC x64 e Windows SDK).' }
    $vs = ([string]($vs | Select-Object -First 1)).Trim()
    $vsVersion = [string](& $vswhere -latest -products '*' -version '[17.0,19.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion | Select-Object -First 1)
    $vsMajor = [int]($vsVersion.Split('.')[0])
    $generator = if ($vsMajor -ge 18) { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' }
    Write-Host "Visual Studio $vsVersion in $vs (generatore: $generator)"

    $stage = 'ambiente MSVC x64'
    $env:SEEDFLOW_DEVCMD = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
    if (!(Test-Path -LiteralPath $env:SEEDFLOW_DEVCMD)) { throw 'VsDevCmd.bat mancante.' }
    $lines = & $env:ComSpec /d /s /c '"call "%SEEDFLOW_DEVCMD%" -no_logo -arch=x64 -host_arch=x64 && set"' 2>&1
    if ($LASTEXITCODE -ne 0) { $lines | ForEach-Object { Write-Host $_ }; throw 'Inizializzazione MSVC fallita.' }
    foreach ($line in $lines) {
        if ([string]$line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
    }
    if (!(Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'Compilatore MSVC x64 non disponibile.' }
    if (!$env:WindowsSdkDir -or !(Test-Path -LiteralPath $env:WindowsSdkDir)) { throw 'Windows SDK mancante.' }
    Write-Host "MSVC $env:VCToolsVersion, Windows SDK $env:WindowsSDKVersion"

    $stage = 'CMake'
    $cmake = $null
    foreach ($candidate in @((Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'), 'cmake.exe')) {
        $found = Find-Exe @($candidate)
        if (!$found) { continue }
        $help = & $found --help 2>&1
        if ($LASTEXITCODE -eq 0 -and ($help -match [regex]::Escape($generator))) { $cmake = $found; break }
    }
    if (!$cmake) { throw "Serve un CMake con il generatore '$generator' (per VS 2026: CMake >= 4.2). Aggiorna CMake tools in Visual Studio." }
    Run $cmake @('--version')

    $stage = 'Git'
    $git = Find-Exe @('git.exe', "$env:ProgramFiles\Git\cmd\git.exe", "$env:LOCALAPPDATA\Programs\Git\cmd\git.exe")
    if (!$git) { throw 'Installa Git per Windows (serve a scaricare vcpkg).' }
    $env:PATH = (Split-Path $git -Parent) + ';' + $env:PATH

    $env:VCPKG_ROOT = [IO.Path]::GetFullPath((Select-VcpkgRoot $requestedVcpkgRoot (Join-Path $root '.build-tools\vcpkg')))
    Write-Host "vcpkg: $env:VCPKG_ROOT"
    if ($CheckOnly) { Write-Host 'PREREQUISITI VERIFICATI. Nessuna compilazione eseguita.'; exit 0 }

    $stage = 'vcpkg'
    $env:VCPKG_DEFAULT_TRIPLET = $triplet
    $env:VCPKG_VISUAL_STUDIO_PATH = $vs
    if (!(Test-Path -LiteralPath $env:VCPKG_ROOT)) { Run $git @('clone', '--depth', '1', 'https://github.com/microsoft/vcpkg.git', $env:VCPKG_ROOT) }
    $vcpkg = Join-Path $env:VCPKG_ROOT 'vcpkg.exe'
    if (!(Test-Path -LiteralPath $vcpkg)) { Run (Join-Path $env:VCPKG_ROOT 'bootstrap-vcpkg.bat') @('-disableMetrics') }
    if (!(Test-Path -LiteralPath (Join-Path $env:VCPKG_ROOT 'ports\libtorrent\vcpkg.json'))) { throw 'Port vcpkg libtorrent assente: aggiorna il checkout vcpkg (git pull).' }

    $stage = 'libtorrent (statico) e dipendenze'
    # core: senza WebTorrent; deprfun: API compatibili (TORRENT_ABI_VERSION=2).
    # La prima volta compila Boost/OpenSSL/libtorrent statici: puo' richiedere molto tempo.
    Run $vcpkg @('install', "libtorrent[core,deprfun]:$triplet", '--classic')

    $stage = 'configurazione CMake'
    $buildName = if ($SharedLibraries) { 'build-win32-shared' } else { 'build-win32' }
    $build = Join-Path $root $buildName
    $toolchain = Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'
    Run $cmake @('-S', $root, '-B', $build, '-G', $generator, '-A', 'x64', "-DCMAKE_GENERATOR_INSTANCE=$vs",
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain", "-DVCPKG_TARGET_TRIPLET=$triplet",
        "-DSF_STATIC_RUNTIME=$(if ($SharedLibraries) { 'OFF' } else { 'ON' })",
        "-DSF_BUILD_TESTS=$(if ($RunTests) { 'ON' } else { 'OFF' })",
        "-DSF_BUILD_ENGINE_TESTS=$(if ($RunTests) { 'ON' } else { 'OFF' })")

    $stage = 'compilazione'
    Run $cmake @('--build', $build, '--config', $config, '--parallel')
    $exe = Join-Path $build "$config\SeedFlow.exe"
    if (!(Test-Path -LiteralPath $exe)) { throw 'SeedFlow.exe non prodotto.' }

    $stage = 'pacchetto'
    $out = Join-Path $root ('SeedFlow-Pronto\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Path $out -Force | Out-Null
    Copy-Item -LiteralPath $exe -Destination $out
    if ($SharedLibraries) {
        Get-ChildItem -LiteralPath (Join-Path $env:VCPKG_ROOT "installed\$triplet\bin") -Filter '*.dll' -File | Copy-Item -Destination $out
        $crt = Get-ChildItem -LiteralPath (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' | Sort-Object Name -Descending | Select-Object -First 1
        if (!$crt) { throw 'Runtime MSVC x64 non trovato.' }
        Get-ChildItem -LiteralPath $crt.FullName -Filter '*.dll' -File | Copy-Item -Destination $out
        if ($RunTests) {
            Get-ChildItem -LiteralPath $out -Filter '*.dll' -File | Copy-Item -Destination (Join-Path $build $config) -Force
        }
    }
    if ($RunTests) {
        Run (Join-Path (Split-Path $cmake -Parent) 'ctest.exe') @('--test-dir', $build, '-C', $config, '--output-on-failure')
    }
    foreach ($doc in @('README.md', 'BUILDING.md', 'BUILD-STATIC-RECIPE.txt', 'CHANGELOG.txt', 'LICENSE', 'VERIFICA.txt')) { Copy-Item -LiteralPath (Join-Path $root $doc) -Destination $out }
    if (Test-Path -LiteralPath (Join-Path $root 'docs')) {
        Copy-Item -LiteralPath (Join-Path $root 'docs') -Destination $out -Recurse
    }
    if (Test-Path -LiteralPath (Join-Path $root 'licenses')) {
        Copy-Item -LiteralPath (Join-Path $root 'licenses') -Destination $out -Recurse
    }
    $size = [math]::Round((Get-Item (Join-Path $out 'SeedFlow.exe')).Length / 1MB, 1)
    Write-Host "COMPILAZIONE COMPLETATA: $out\SeedFlow.exe ($size MB; triplet $triplet)"
    exit 0
} catch {
    Write-Host "ERRORE nella fase: $stage"
    Write-Host $_.Exception.Message
    Write-Host "COMPILAZIONE NON RIUSCITA - log: $log"
    exit 1
} finally {
    if ($started) { Stop-Transcript | Out-Null }
}

