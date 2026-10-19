$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vcvars = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Auxiliary\Build\vcvars64.bat' | Select-Object -First 1
if (-not $vcvars) { throw 'MSVC C build tools not found' }
$testOutput = Join-Path $projectRoot 'Output\host-tests'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$sources = @((Join-Path $projectRoot 'Tests\test_motor.c'), (Join-Path $projectRoot 'Tests\test_ui.c'), (Join-Path $projectRoot 'User\app\motor_ui.c'), (Join-Path $projectRoot 'User\fonts\font16.c')) + @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'User\motor') -Filter '*.c' | ForEach-Object { $_.FullName })
$sourceArgs = ($sources | ForEach-Object { '"' + $_ + '"' }) -join ' '
$command = 'call "' + $vcvars + '" >nul && cd /d "' + $testOutput + '" && cl /nologo /W4 /WX /std:c11 /D_CRT_SECURE_NO_WARNINGS /I"' + (Join-Path $projectRoot 'Tests\stubs') + '" /I"' + (Join-Path $projectRoot 'User') + '" ' + $sourceArgs + ' /Fe:motor_tests.exe && motor_tests.exe'
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'Motor host tests failed' }
