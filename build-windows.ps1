param(
    [Parameter(Mandatory = $true)][string]$Disc,
    [string]$BuildRoot = (Join-Path $PSScriptRoot 'build'),
    [string]$SevenZip = '7z'
)

$ErrorActionPreference = 'Stop'
$Base = Join-Path $PSScriptRoot 'vendor/ps2-base'
$Base = (Resolve-Path -LiteralPath $Base).Path
$Disc = (Resolve-Path -LiteralPath $Disc).Path
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$Prepared = Join-Path $BuildRoot 'disc-work'
$CMakeBuild = Join-Path $BuildRoot 'cmake'
$Recompiler = Join-Path $CMakeBuild 'ps2xRecomp/Release/ps2_recomp.exe'
$RuntimeSource = Join-Path $Base 'PS2Recomp'
$Hook = Join-Path $PSScriptRoot 'tools/desktop.cmake'

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed ($LASTEXITCODE)." }
}

$config = @(
    '-S', $RuntimeSource, '-B', $CMakeBuild, '-G', 'Visual Studio 17 2022', '-A', 'x64',
    "-DCMAKE_PROJECT_PS2RetroX_INCLUDE=$Hook",
    '-DPS2X_BUILD_TEST=OFF', '-DPS2X_BUILD_STUDIO=OFF', '-DPS2X_BUILD_ANALYZER=OFF',
    '-DPS2X_ENABLE_DEBUG_UI=OFF', '-DPS2X_ENABLE_RUNNER_PCH=OFF',
    '-DPS2X_ENABLE_AGRESSIVE_LOGS=OFF', '-DPS2X_ENABLE_IOP_RPC_TRACE=OFF',
    '-DPS2X_ENABLE_SCCACHE=OFF', '-DPS2X_MSVC_MP_PROCESSES=1'
)

if (Test-Path -LiteralPath $Prepared) {
    throw "Escolha outro -BuildRoot ou remova a pasta de compilacao que voce gerou: $Prepared"
}
Invoke-Checked cmake ($config + @('-DPS2X_BUILD_RUNTIME=OFF', '-DBARBIE_BATCH_DIR='))
Invoke-Checked cmake @('--build', $CMakeBuild, '--config', 'Release', '--target', 'ps2_recomp', '--parallel', '2')
Invoke-Checked python @('-B', (Join-Path $PSScriptRoot 'tools/prepare.py'), $Disc, $Recompiler, $Prepared, '--7z', $SevenZip)
Invoke-Checked cmake ($config + @(
    '-DPS2X_BUILD_RUNTIME=ON',
    "-DBARBIE_BATCH_DIR=$(Join-Path $Prepared 'batches')",
    "-DBARBIE_GENERATED_DIR=$(Join-Path $Prepared 'generated')",
    "-DBARBIE_SOURCE_DIR=$(Join-Path $PSScriptRoot 'src')"
))
Invoke-Checked cmake @('--build', $CMakeBuild, '--config', 'Release', '--target', 'Barbie12PC', '--parallel', '2')
Copy-Item -LiteralPath (Join-Path $CMakeBuild 'Release/Barbie12PC.exe') -Destination $Prepared
Write-Output "Execute .\play-windows.ps1 -Prepared '$Prepared'"
