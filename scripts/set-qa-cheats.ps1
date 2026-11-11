[CmdletBinding()]
param(
    [Parameter(Mandatory)][bool]$Enabled,
    [string]$Serial = '',
    [string]$Adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Package = 'io.github.raposomiguel50.systemshock.qa'
$DeviceArgs = @()
if ($Serial) { $DeviceArgs = @('-s', $Serial) }
& $Adb @DeviceArgs get-state
if ($LASTEXITCODE -ne 0) { throw 'Connect one authorized device, or supply -Serial.' }
$Running = & $Adb @DeviceArgs shell pidof $Package
if ($Running) { throw 'Save your game and stop the QA app before changing its preferences. This script does not interrupt a running game.' }
$Lines = & $Adb @DeviceArgs exec-out run-as $Package cat files/prefs.txt
if ($LASTEXITCODE -ne 0) { throw 'Launch the QA app once to create files/prefs.txt, then stop it and retry.' }
$Root = Split-Path -Parent $PSScriptRoot
$BackupDir = Join-Path $Root 'dist/qa1-preferences'
New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null
$Backup = Join-Path $BackupDir ("prefs-{0}.txt" -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$Lines | Set-Content -LiteralPath $Backup -Encoding utf8NoBOM
$Updated = @($Lines | Where-Object { $_ -notmatch '^\s*qa_cheats\s*=' })
$Updated += "qa_cheats = $($Enabled.ToString().ToLowerInvariant())"
$Updated | & $Adb @DeviceArgs shell run-as $Package tee files/prefs.txt | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Preference write failed. Original preferences are backed up at $Backup" }
$Verify = & $Adb @DeviceArgs exec-out run-as $Package cat files/prefs.txt
if ($LASTEXITCODE -ne 0 -or $Verify -notcontains $Updated[-1]) { throw 'Preference verification failed.' }
Write-Host "QA_CHEATS=$Enabled"
Write-Host "BACKUP=$Backup"
Write-Host 'Start the QA app, enter a game, then tap SET > QA TOOLS.'
