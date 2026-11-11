[CmdletBinding()]
param(
    [ValidateSet('debug','qa','releaseQa','release')][string]$Variant = 'debug',
    [string]$Apk = '',
    [string]$AndroidSdk = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $PSScriptRoot
$ExpectedVersionCode = '10000'
$BaseVersionName = '1.0.0'
$ExpectedApplicationLabel = 'System Shock - Android'
$StablePackage = 'io.github.raposomiguel50.systemshock'

if (-not $AndroidSdk) {
    if ($env:ANDROID_SDK_ROOT) { $AndroidSdk = $env:ANDROID_SDK_ROOT }
    elseif ($env:ANDROID_HOME) { $AndroidSdk = $env:ANDROID_HOME }
    elseif ($env:LOCALAPPDATA) { $AndroidSdk = Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
}
if (-not (Test-Path -LiteralPath $AndroidSdk -PathType Container)) {
    throw "Android SDK not found: $AndroidSdk"
}

$VariantInfo = switch ($Variant) {
    'debug' {
        [pscustomobject]@{
            Apk = 'AndroidProject\app\build\outputs\apk\debug\app-debug.apk'
            Package = $StablePackage
            VersionName = $BaseVersionName
            Debuggable = $true
            ReleaseSigned = $false
        }
    }
    'qa' {
        [pscustomobject]@{
            Apk = 'AndroidProject\app\build\outputs\apk\qa\app-qa.apk'
            Package = "$StablePackage.qa"
            VersionName = "$BaseVersionName-qa"
            Debuggable = $true
            ReleaseSigned = $false
        }
    }
    'releaseQa' {
        [pscustomobject]@{
            Apk = 'AndroidProject\app\build\outputs\apk\releaseQa\app-releaseQa.apk'
            Package = "$StablePackage.releaseqa"
            VersionName = "$BaseVersionName-releaseqa"
            Debuggable = $false
            ReleaseSigned = $true
        }
    }
    'release' {
        [pscustomobject]@{
            Apk = 'AndroidProject\app\build\outputs\apk\release\app-release.apk'
            Package = $StablePackage
            VersionName = $BaseVersionName
            Debuggable = $false
            ReleaseSigned = $true
        }
    }
}

if (-not $Apk) {
    $Apk = Join-Path $Root $VariantInfo.Apk
}
if (-not (Test-Path -LiteralPath $Apk -PathType Leaf)) {
    throw "APK not found: $Apk"
}

$Aapt2 = Join-Path $AndroidSdk 'build-tools\34.0.0\aapt2.exe'
$ApkSigner = Join-Path $AndroidSdk 'build-tools\34.0.0\apksigner.bat'
foreach ($ToolPath in @($Aapt2,$ApkSigner)) {
    if (-not (Test-Path -LiteralPath $ToolPath -PathType Leaf)) {
        throw "Required Android build tool not found: $ToolPath"
    }
}

$ZipAlign = Get-ChildItem -LiteralPath (Join-Path $AndroidSdk 'build-tools') -Directory |
    Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' -and [version]$_.Name -ge [version]'35.0.0' } |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName 'zipalign.exe' } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $ZipAlign) {
    throw 'Android SDK build-tools 35.0.0 or newer are required for zipalign -P 16 verification.'
}
$ReadElf = Join-Path $AndroidSdk 'ndk\29.0.14206865\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-readelf.exe'
if (-not (Test-Path -LiteralPath $ReadElf -PathType Leaf)) {
    throw "NDK llvm-readelf not found: $ReadElf"
}

$ApkAnalyzer = Join-Path $AndroidSdk 'cmdline-tools\latest\bin\apkanalyzer.bat'
if (-not (Test-Path -LiteralPath $ApkAnalyzer -PathType Leaf)) {
    $ApkAnalyzer = $null
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$Zip = [IO.Compression.ZipFile]::OpenRead($Apk)
try {
    $Names = @($Zip.Entries | ForEach-Object FullName)
    $RequiredLibraries = @('lib/arm64-v8a/libmain.so','lib/arm64-v8a/libSDL2.so','lib/arm64-v8a/libSDL2_mixer.so')
    foreach ($RequiredLibrary in $RequiredLibraries) {
        if ($Names -notcontains $RequiredLibrary) {
            throw "Required ARM64 library missing: $RequiredLibrary"
        }
    }

    $ProprietaryMatches = @($Names | Where-Object { $_ -match '(?i)(^|/)(res/data|res/sound)/|\.res$|archive\.dat$' })
    if ($ProprietaryMatches.Count -gt 0) {
        throw "Potential proprietary game data found in APK: $($ProprietaryMatches -join ', ')"
    }

    $UnexpectedAbiEntries = @($Names | Where-Object { $_ -match '^lib/(?!arm64-v8a/)[^/]+/' })
    if ($UnexpectedAbiEntries.Count -gt 0) {
        throw "Unexpected non-ARM64 libraries found: $($UnexpectedAbiEntries -join ', ')"
    }
    $NativeLibraries = @($Names | Where-Object { $_ -match '^lib/arm64-v8a/[^/]+\.so$' })
}
finally {
    $Zip.Dispose()
}

$ZipAlignmentLines = @(& $ZipAlign -c -P 16 -v 4 $Apk 2>&1 | ForEach-Object { $_.ToString() })
if ($LASTEXITCODE -ne 0) {
    throw "APK 16 KB ZIP alignment failed:`n$($ZipAlignmentLines -join "`n")"
}
foreach ($NativeLibrary in $NativeLibraries) {
    $LibraryZipLine = @($ZipAlignmentLines | Where-Object { $_ -match "\s$([regex]::Escape($NativeLibrary))\s+\(OK\)$" })
    if ($LibraryZipLine.Count -ne 1) {
        throw "Uncompressed 16 KB-aligned APK library not confirmed: $NativeLibrary"
    }
}

$TempLibraries = Join-Path ([IO.Path]::GetTempPath()) ("systemshock-apk-elf-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $TempLibraries | Out-Null
try {
    $Zip = [IO.Compression.ZipFile]::OpenRead($Apk)
    try {
        foreach ($NativeLibrary in $NativeLibraries) {
            $Entry = $Zip.GetEntry($NativeLibrary)
            $TempLibrary = Join-Path $TempLibraries ([IO.Path]::GetFileName($NativeLibrary))
            $InputStream = $Entry.Open()
            try {
                $OutputStream = [IO.File]::Create($TempLibrary)
                try { $InputStream.CopyTo($OutputStream) }
                finally { $OutputStream.Dispose() }
            }
            finally { $InputStream.Dispose() }

            $ProgramHeaders = @(& $ReadElf -lW $TempLibrary 2>&1 | ForEach-Object { $_.ToString() })
            if ($LASTEXITCODE -ne 0) {
                throw "llvm-readelf failed for $NativeLibrary`: $($ProgramHeaders -join "`n")"
            }
            $LoadAlignments = @($ProgramHeaders | ForEach-Object {
                if ($_ -match '^\s*LOAD\s+.*\s+(0x[0-9a-fA-F]+)\s*$') { $Matches[1] }
            })
            if ($LoadAlignments.Count -eq 0) {
                throw "No PT_LOAD segments found in $NativeLibrary"
            }
            foreach ($Alignment in $LoadAlignments) {
                if ([Convert]::ToInt64($Alignment.Substring(2), 16) -lt 16384) {
                    throw "$NativeLibrary has PT_LOAD alignment $Alignment; 0x4000 or greater is required."
                }
            }
            Write-Host "ELF_PT_LOAD=$NativeLibrary`: $($LoadAlignments -join ',')"
        }
    }
    finally { $Zip.Dispose() }
}
finally { Remove-Item -LiteralPath $TempLibraries -Recurse -Force }
Write-Host 'NATIVE_16KB_ZIP_ALIGNMENT=PASS'
Write-Host 'NATIVE_16KB_ELF_ALIGNMENT=PASS'

$BadgingLines = @(& $Aapt2 dump badging $Apk 2>&1 | ForEach-Object { $_.ToString() })
if ($LASTEXITCODE -ne 0) {
    throw "aapt2 badging failed: $LASTEXITCODE"
}
$BadgingText = $BadgingLines -join "`n"

if ($BadgingText -notmatch "package:\s+name='(?<package>[^']+)'\s+versionCode='(?<versionCode>[^']+)'\s+versionName='(?<versionName>[^']*)'") {
    throw 'Unable to parse package metadata from APK.'
}
$ActualPackage = $Matches.package
$ActualVersionCode = $Matches.versionCode
$ActualVersionName = $Matches.versionName

if ($ActualPackage -ne $VariantInfo.Package) {
    throw "Package mismatch. Expected $($VariantInfo.Package), found $ActualPackage"
}
if ($ActualVersionCode -ne $ExpectedVersionCode) {
    throw "versionCode mismatch. Expected $ExpectedVersionCode, found $ActualVersionCode"
}
if ($ActualVersionName -ne $VariantInfo.VersionName) {
    throw "versionName mismatch. Expected $($VariantInfo.VersionName), found $ActualVersionName"
}
if ($BadgingText -notmatch "application-label:'(?<label>[^']*)'") {
    throw 'Unable to parse application label from APK.'
}
$ActualApplicationLabel = $Matches.label
if ($ActualApplicationLabel -ne $ExpectedApplicationLabel) {
    throw "Application label mismatch. Expected '$ExpectedApplicationLabel', found '$ActualApplicationLabel'"
}
if ($BadgingText -notmatch "sdkVersion:'33'") {
    throw 'minSdk mismatch. Expected 33.'
}
if ($BadgingText -notmatch "targetSdkVersion:'33'") {
    throw 'targetSdk mismatch. Expected 33.'
}
if ($BadgingText -notmatch "native-code:\s+'arm64-v8a'") {
    throw 'Native ABI mismatch. Expected arm64-v8a only.'
}

$ManifestLines = @(& $Aapt2 dump xmltree --file AndroidManifest.xml $Apk 2>&1 | ForEach-Object { $_.ToString() })
if ($LASTEXITCODE -ne 0) {
    throw "aapt2 xmltree failed: $LASTEXITCODE"
}
$IconLines = @($ManifestLines | Select-String -Pattern ':icon\(')
if ($IconLines.Count -lt 1) {
    throw 'Compiled launcher icon is missing from AndroidManifest.xml.'
}
$LauncherLines = @($ManifestLines | Select-String -Pattern 'LauncherActivity')
if ($LauncherLines.Count -lt 1) {
    throw 'Compiled launcher activity is missing from AndroidManifest.xml.'
}

$DebuggableSource = 'aapt2'
$IsDebuggable = $false
if ($ApkAnalyzer) {
    $DebuggableOutput = (& $ApkAnalyzer manifest debuggable $Apk 2>&1 | Out-String).Trim().ToLowerInvariant()
    if ($LASTEXITCODE -ne 0) {
        throw "apkanalyzer manifest debuggable failed: $LASTEXITCODE"
    }
    if ($DebuggableOutput -notin @('true','false')) {
        throw "Unable to parse apkanalyzer debuggable output: $DebuggableOutput"
    }
    $IsDebuggable = [bool]::Parse($DebuggableOutput)
    $DebuggableSource = 'apkanalyzer'
}
else {
    $DebuggableLines = @($ManifestLines | Select-String -Pattern ':debuggable\(')
    if ($DebuggableLines.Count -eq 0) {
        $IsDebuggable = $false
    }
    elseif ($DebuggableLines.Count -eq 1) {
        $DebuggableText = $DebuggableLines[0].ToString()
if (
    $DebuggableText -match '\(type\s+0x12\)0xffffffff(?:\s|$)' -or
    $DebuggableText -match '=true(?:\s|$)'
) {
    $IsDebuggable = $true
}
elseif (
    $DebuggableText -match '\(type\s+0x12\)0x0(?:\s|$)' -or
    $DebuggableText -match '=false(?:\s|$)'
) {
    $IsDebuggable = $false
}
else {
    throw "Unable to parse AAPT2 debuggable boolean: $DebuggableText"
}
    }
    else {
        throw "Unexpected number of debuggable attributes in compiled manifest: $($DebuggableLines.Count)"
    }
}

if ($IsDebuggable -ne $VariantInfo.Debuggable) {
    throw "Debuggable flag mismatch for variant '$Variant'. Expected $($VariantInfo.Debuggable), found $IsDebuggable"
}

$SignerLines = @(& $ApkSigner verify --verbose --print-certs $Apk 2>&1 | ForEach-Object { $_.ToString() })
if ($LASTEXITCODE -ne 0) {
    throw "APK signature verification failed: $LASTEXITCODE"
}
$SignerText = $SignerLines -join "`n"
if ($SignerText -notmatch 'Signer #1 certificate SHA-256 digest:\s*(?<digest>[0-9a-fA-F]+)') {
    throw 'Unable to read APK signing certificate SHA-256 digest.'
}
$SignerDigest = $Matches.digest.ToLowerInvariant()
$SignerDn = ''
if ($SignerText -match 'Signer #1 certificate DN:\s*(?<dn>.+)') {
    $SignerDn = $Matches.dn.Trim()
}

if ($VariantInfo.ReleaseSigned) {
    if ($SignerDn -match 'CN=Android Debug') {
        throw "$Variant APK is signed with the generic Android debug certificate."
    }

    $ExpectedReleaseDigest = [Environment]::GetEnvironmentVariable('RP5NP_RELEASE_CERT_SHA256')
    if ([string]::IsNullOrWhiteSpace($ExpectedReleaseDigest)) {
        throw 'RP5NP_RELEASE_CERT_SHA256 must pin the stable release certificate for release verification.'
    }
    $NormalizedExpectedDigest = ($ExpectedReleaseDigest -replace '[^0-9a-fA-F]','').ToLowerInvariant()
    if ($SignerDigest -ne $NormalizedExpectedDigest) {
        throw "Stable release signing certificate mismatch. Expected $NormalizedExpectedDigest, found $SignerDigest"
    }
}

Write-Host 'APK_VERIFY=PASS'
Write-Host "VARIANT=$Variant"
Write-Host "PACKAGE=$ActualPackage"
Write-Host "VERSION_CODE=$ActualVersionCode"
Write-Host "VERSION_NAME=$ActualVersionName"
Write-Host "APPLICATION_LABEL=$ActualApplicationLabel"
Write-Host "DEBUGGABLE=$IsDebuggable"
Write-Host "DEBUGGABLE_SOURCE=$DebuggableSource"
Write-Host "SIGNER_SHA256=$SignerDigest"
Write-Host "SHA256=$((Get-FileHash -LiteralPath $Apk -Algorithm SHA256).Hash.ToLowerInvariant())"
