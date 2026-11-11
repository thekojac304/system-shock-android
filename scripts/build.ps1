[CmdletBinding()]
param(
    [ValidateSet('debug','qa','releaseQa','release')][string]$Variant = 'debug',
    [string]$AndroidSdk = '',
    [string]$JavaHome = $env:JAVA_HOME,
    [string]$DepsRoot = (Join-Path (Split-Path -Parent $PSScriptRoot) '.deps'),
    [switch]$SkipGitCleanCheck,
    [switch]$AudioDiagnostics
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($AudioDiagnostics -and $Variant -ne 'qa') {
    throw 'AudioDiagnostics is restricted to the separate QA application.'
}

$Root = Split-Path -Parent $PSScriptRoot
$Preflight = Join-Path $PSScriptRoot 'preflight.ps1'

if (-not $AndroidSdk) {
    if ($env:ANDROID_SDK_ROOT) { $AndroidSdk = $env:ANDROID_SDK_ROOT }
    elseif ($env:ANDROID_HOME) { $AndroidSdk = $env:ANDROID_HOME }
    elseif ($env:LOCALAPPDATA) { $AndroidSdk = Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
}

& $Preflight -Variant $Variant -AndroidSdk $AndroidSdk -JavaHome $JavaHome -DepsRoot $DepsRoot -SkipGitCleanCheck:$SkipGitCleanCheck

# The QA generated-light assets are compiled from the user's ignored, owned
# archive and accepted diagnostics. Never package a stale prior compilation.
if ($Variant -eq 'qa') {
    $GeneratedInputs = @(
        (Join-Path $Root 'dist\audio2\owned-ARCHIVE.DAT'),
        (Join-Path $Root 'dist\horror-author1d\generated\level-0.generated.json'),
        (Join-Path $Root 'dist\horror-author1d\generated\level-1.generated.json'),
        (Join-Path $Root 'dist\horror-author1d\generated\level-2.generated.json'),
        (Join-Path $Root 'dist\horror-author1d\generated\level-4.generated.json'),
        (Join-Path $Root 'dist\horror-author1d\generated\level-5.generated.json'),
        (Join-Path $Root 'dist\horror-author1d\generated\level-6.generated.json'),
        (Join-Path $Root 'dist\horror-author1d\generated\level-7.generated.json'),
        (Join-Path $Root 'dist\horror-author1d\generated\level-9.generated.json'),
        (Join-Path $Root 'dist\horror-author1e\level-0.partial.json'),
        (Join-Path $Root 'dist\horror-author1e\level-1.partial.json'),
        (Join-Path $Root 'dist\horror-author1e\level-2.partial.json'),
        (Join-Path $Root 'dist\horror-author1e\level-4.partial.json'),
        (Join-Path $Root 'dist\horror-author1e\level-5.partial.json'),
        (Join-Path $Root 'dist\horror-author1e\level-6.partial.json'),
        (Join-Path $Root 'dist\horror-author1e\level-7.partial.json'),
        (Join-Path $Root 'dist\horror-author1e\level-9.partial.json')
    )
    $GeneratedAssets = Join-Path $Root 'AndroidProject\app\src\qa\assets\horror-generated'
    $InputsPresent = @($GeneratedInputs | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }).Count
    if ($InputsPresent -eq $GeneratedInputs.Count) {
        & python (Join-Path $Root 'scripts\compile-horror-generated.py')
        if ($LASTEXITCODE -ne 0) { throw 'Generated-light profile compilation failed.' }
    } elseif (Test-Path -LiteralPath $GeneratedAssets) {
        throw 'Generated-light assets exist without all accepted input files; refusing a stale QA package.'
    }
}

$Sdl = Join-Path $DepsRoot 'SDL2'
$Mixer = Join-Path $DepsRoot 'SDL2_mixer'

$env:JAVA_HOME = $JavaHome
$env:ANDROID_HOME = $AndroidSdk
$env:ANDROID_SDK_ROOT = $AndroidSdk
$env:RP5NP_SDL2_SOURCE_DIR = $Sdl
$env:RP5NP_SDL2_MIXER_SOURCE_DIR = $Mixer

$VariantInfo = switch ($Variant) {
    'debug' {
        [pscustomobject]@{
            Task = ':app:assembleDebug'
            Apk = 'app\build\outputs\apk\debug\app-debug.apk'
        }
    }
    'qa' {
        [pscustomobject]@{
            Task = ':app:assembleQa'
            Apk = 'app\build\outputs\apk\qa\app-qa.apk'
        }
    }
    'releaseQa' {
        [pscustomobject]@{
            Task = ':app:assembleReleaseQa'
            Apk = 'app\build\outputs\apk\releaseQa\app-releaseQa.apk'
        }
    }
    'release' {
        [pscustomobject]@{
            Task = ':app:assembleRelease'
            Apk = 'app\build\outputs\apk\release\app-release.apk'
        }
    }
}

$AndroidProject = Join-Path $Root 'AndroidProject'
Push-Location $AndroidProject
try {
    & .\gradlew.bat --no-daemon $VariantInfo.Task "-Prp5npAudioDiagnostics=$($AudioDiagnostics.IsPresent.ToString().ToLowerInvariant())"
    if ($LASTEXITCODE -ne 0) {
        throw "Gradle build failed: $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

$Apk = Join-Path $AndroidProject $VariantInfo.Apk
if (-not (Test-Path -LiteralPath $Apk -PathType Leaf)) {
    throw "Expected APK not found: $Apk"
}

Write-Host 'BUILD=PASS'
Write-Host "VARIANT=$Variant"
Write-Host "APK=$Apk"
Write-Host "SHA256=$((Get-FileHash -LiteralPath $Apk -Algorithm SHA256).Hash.ToLowerInvariant())"
