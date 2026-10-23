$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$vswhere='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vcvars=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Auxiliary\Build\vcvars64.bat' | Select-Object -First 1
if (-not $vcvars) { throw 'MSVC C build tools not found' }
$testOutput=Join-Path $projectRoot 'Output\host-tests'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$sources=@('Tests\test_chassis.c','User\motor\emm_protocol.c','User\motor\motor_bus.c') | ForEach-Object { Join-Path $projectRoot $_ }
$sources+=@(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'User\chassis') -Filter '*.c' | ForEach-Object { $_.FullName })
$sourceArgs=($sources | ForEach-Object { '"'+$_+'"' }) -join ' '
foreach ($strict in 0,1) {
    $command='call "'+$vcvars+'" >nul && cd /d "'+$testOutput+'" && cl /nologo /W4 /WX /std:c11 /D_CRT_SECURE_NO_WARNINGS /DMOTOR_DRIVER_REQUIRE_MANUAL='+$strict+' /I"'+(Join-Path $projectRoot 'User')+'" '+$sourceArgs+' /Fe:chassis_tests.exe && chassis_tests.exe'
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw 'Chassis tests failed' }
}
