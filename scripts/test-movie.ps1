[CmdletBinding()]
param([string]$Compiler = 'g++', [string]$Movie = '')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$CompilerBin = Split-Path -Parent (Get-Command $Compiler -ErrorAction Stop).Source
$Output = Join-Path $Root 'dist/movie-tests'
New-Item -ItemType Directory -Force -Path $Output | Out-Null
foreach ($Optimization in @('0', '2')) {
    $Exe = Join-Path $Output "draw4x4-O$Optimization.exe"
    & $Compiler "-B$CompilerBin/" -std=c++11 -Wall -Wextra -Werror "-O$Optimization" `
        "-I$(Join-Path $Root 'tests/movie/stubs')" (Join-Path $Root 'tests/movie/draw4x4_test.cpp') -o $Exe
    if ($LASTEXITCODE -ne 0) { throw 'Movie test compilation failed' }
    if ($Movie) { & $Exe $Movie } else { & $Exe }
    if ($LASTEXITCODE -ne 0) { throw "Movie tests failed: $LASTEXITCODE" }
}
