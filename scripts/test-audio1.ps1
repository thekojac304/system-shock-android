[CmdletBinding()]
param([string]$Compiler = 'gcc')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$CompilerBin = Split-Path -Parent (Get-Command $Compiler -ErrorAction Stop).Source
$env:PATH = "$CompilerBin;$env:PATH"
$Output = Join-Path $Root 'dist/audio1-tests'
New-Item -ItemType Directory -Force -Path $Output | Out-Null
foreach ($Mode in @('mixer', 'no-mixer')) {
    $Defines = @('-DDECLSPEC=', '-DSDL_MAIN_HANDLED')
    if ($Mode -eq 'mixer') { $Defines += '-DUSE_SDL_MIXER' }
    $Exe = Join-Path $Output "external_music_$Mode.exe"
    & $Compiler "-B$CompilerBin/" -std=c99 -Wall -Wextra -Werror -pedantic -O2 @Defines `
        "-I$Root/.deps/SDL2/include" "-I$Root/.deps/SDL2_mixer/include" `
        "-I$Root/SourceSnapshot/src/MacSrc" `
        "$Root/SourceSnapshot/src/MacSrc/ExternalMusic.c" "$Root/tests/audio/external_music_test.c" -o $Exe
    if ($LASTEXITCODE -ne 0) { throw "AUDIO1 $Mode compilation failed" }
    & $Exe
    if ($LASTEXITCODE -ne 0) { throw "AUDIO1 $Mode tests failed" }
}
