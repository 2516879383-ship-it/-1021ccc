param([switch]$Rebuild)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$projectFile = Join-Path $projectRoot 'Project\YH-F429.uvprojx'
$logFile = Join-Path $projectRoot 'Output\build.log'
$buildMode = if ($Rebuild) { '-r' } else { '-b' }
$process = Start-Process -FilePath 'C:\Keil_v5\UV4\UV4.exe' -WorkingDirectory $projectRoot -ArgumentList @($buildMode, ('"'+$projectFile+'"'), '-t', 'LCD', '-o', ('"'+$logFile+'"')) -WindowStyle Hidden -Wait -PassThru
$log = Get-Content -LiteralPath $logFile
$log | Select-Object -Last 15
if ($process.ExitCode -gt 1 -or -not ($log -match '0 Error\(s\)')) { throw 'Keil build failed; see Output/build.log' }
$unexpected = $log | Where-Object { $_ -match 'warning:' -and $_ -notmatch 'gt9xx.c\(\d+\): warning:  #177-D:' }
if ($unexpected) { $unexpected; throw 'New compiler warning' }
Write-Output 'BUILD PASS (baseline: 8 existing gt9xx unused-symbol warnings).'
