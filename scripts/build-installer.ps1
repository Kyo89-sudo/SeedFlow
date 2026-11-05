param([string]$CMake)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$CMake) {
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($command) { $CMake = $command.Source }
    else {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            $CMake = & $vswhere -latest -products '*' -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' | Select-Object -First 1
        }
    }
}
if (!$CMake) { throw 'CMake non trovato.' }
$build = Join-Path $root 'build-win32'
if (!(Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt'))) {
    throw 'Compilare prima SeedFlow con build.bat, poi eseguire questo script.'
}
& $CMake -S $root -B $build -DSF_BUILD_INSTALLER=ON
if ($LASTEXITCODE -ne 0) { throw 'Configurazione installer fallita.' }
& $CMake --build $build --config Release --target SeedFlowSetup --parallel 1
if ($LASTEXITCODE -ne 0) { throw 'Compilazione installer fallita.' }
$version = [regex]::Match([IO.File]::ReadAllText((Join-Path $root 'src\version.h')), '#define SF_VERSION_STRING "([0-9.]+)"').Groups[1].Value
if (!$version) { throw 'Versione non trovata.' }
$built = Join-Path $build 'Release\SeedFlowSetup.exe'
if ((Get-Item -LiteralPath $built).VersionInfo.FileVersion -ne $version) { throw 'Versione installer incoerente.' }
$out = Join-Path $root 'SeedFlow-Pronto'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$setup = Join-Path $out "SeedFlow-$version-win64-setup.exe"
Copy-Item -LiteralPath $built -Destination $setup -Force
$hash = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$setup.sha256", "$hash  $([IO.Path]::GetFileName($setup))" + [Environment]::NewLine)
Write-Host "Installer pronto: $setup"
