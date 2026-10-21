$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vcvars = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Auxiliary\Build\vcvars64.bat' | Select-Object -First 1
if (-not $vcvars) { throw 'MSVC C build tools not found' }
$out = Join-Path $projectRoot 'Output\host-flow'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$sources = @('Tests\test_flow.c','User\flow\pmw3901.c','User\flow\flow_service.c') | ForEach-Object { '"' + (Join-Path $projectRoot $_) + '"' }
$base = 'call "' + $vcvars + '" >nul && cd /d "' + $out + '" && cl /nologo /W4 /WX /std:c11 /D_CRT_SECURE_NO_WARNINGS /I"' + (Join-Path $projectRoot 'User') + '" '
$command = $base + ($sources -join ' ') + ' /Fe:flow_tests.exe && flow_tests.exe'
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'Flow host tests failed' }
$command = $base + '/DFLOW_SWAP_XY=1 /DFLOW_X_SIGN=-1 /DFLOW_Y_SIGN=-1 ' + ($sources -join ' ') + ' /Fe:flow_mapped_tests.exe && flow_mapped_tests.exe'
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'Mapped flow tests failed' }

$spiSources = @('Tests\test_flow_spi.c','User\flow\bsp_flow_spi.c','User\flow\pmw3901.c') | ForEach-Object { '"' + (Join-Path $projectRoot $_) + '"' }
$command = $base + '/I"' + (Join-Path $projectRoot 'Tests\flow_stubs') + '" ' + ($spiSources -join ' ') + ' /Fe:flow_spi_tests.exe && flow_spi_tests.exe'
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'Flow SPI BSP tests failed' }
