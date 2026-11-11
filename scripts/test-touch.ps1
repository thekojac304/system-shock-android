[CmdletBinding()]
param([string]$Compiler = 'gcc')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$CompilerBin = Split-Path -Parent (Get-Command $Compiler -ErrorAction Stop).Source
$env:PATH = "$CompilerBin;$env:PATH"
$Root = Split-Path -Parent $PSScriptRoot
$Touch = Join-Path $Root 'AndroidProject/app/jni/touch'
$Core = @((Join-Path $Touch 'touch_controls.c'), (Join-Path $Touch 'touch_settings.c'), (Join-Path $Touch 'touch_editor.c'), (Join-Path $Touch 'touch_radial.c'))
$Output = Join-Path $Root 'dist/touch-tests'
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$QaExe = Join-Path $Output 'touch_qa_test.exe'
& $Compiler "-B$CompilerBin/" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -DTOUCH_HEADLESS "-I$Touch" `
    @Core (Join-Path $Touch 'tests/touch_qa_test.c') -lm -o $QaExe
if ($LASTEXITCODE -ne 0) { throw "QA touch test compilation failed: $LASTEXITCODE" }
& $QaExe
if ($LASTEXITCODE -ne 0) { throw "QA touch tests failed: $LASTEXITCODE" }
$Exe = Join-Path $Output 'touch_controls_test.exe'
& $Compiler "-B$CompilerBin/" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -DTOUCH_HEADLESS "-I$Touch" `
    @Core (Join-Path $Touch 'tests/touch_controls_test.c') -lm -o $Exe
if ($LASTEXITCODE -ne 0) { throw "Touch test compilation failed: $LASTEXITCODE" }
& $Exe
if ($LASTEXITCODE -ne 0) { throw "Touch tests failed: $LASTEXITCODE" }
$Alpha11Exe = Join-Path $Output 'touch_alpha11_test.exe'
& $Compiler "-B$CompilerBin/" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -DTOUCH_HEADLESS "-I$Touch" `
    @Core (Join-Path $Touch 'tests/touch_alpha11_test.c') -lm -o $Alpha11Exe
if ($LASTEXITCODE -ne 0) { throw "Touch Alpha 1.1 test compilation failed: $LASTEXITCODE" }
& $Alpha11Exe
if ($LASTEXITCODE -ne 0) { throw "Touch Alpha 1.1 tests failed: $LASTEXITCODE" }
$Alpha2Exe = Join-Path $Output 'touch_alpha2_test.exe'
& $Compiler "-B$CompilerBin/" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -DTOUCH_HEADLESS "-I$Touch" `
    @Core (Join-Path $Touch 'tests/touch_alpha2_test.c') -lm -o $Alpha2Exe
if ($LASTEXITCODE -ne 0) { throw "Touch Alpha 2 test compilation failed: $LASTEXITCODE" }
& $Alpha2Exe
if ($LASTEXITCODE -ne 0) { throw "Touch Alpha 2 tests failed: $LASTEXITCODE" }
$SurfaceExe = Join-Path $Output 'touch_surface_test.exe'
& $Compiler "-B$CompilerBin/" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -DTOUCH_HEADLESS -DDECLSPEC= -DSDL_MAIN_HANDLED `
    "-I$(Join-Path $Root '.deps/SDL2/include')" "-I$Touch" (Join-Path $Touch 'tests/touch_surface_test.c') -o $SurfaceExe
if ($LASTEXITCODE -ne 0) { throw "Touch surface test compilation failed: $LASTEXITCODE" }
& $SurfaceExe
if ($LASTEXITCODE -ne 0) { throw "Touch surface tests failed: $LASTEXITCODE" }
$BridgeExe = Join-Path $Output 'touch_bridge_test.exe'
& $Compiler "-B$CompilerBin/" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -DTOUCH_HEADLESS `
    "-I$(Join-Path $Touch 'tests/stubs')" "-I$Touch" `
    @Core `
    (Join-Path $Touch 'tests/touch_bridge_test.c') -lm -o $BridgeExe
if ($LASTEXITCODE -ne 0) { throw "Touch bridge test compilation failed: $LASTEXITCODE" }
& $BridgeExe
if ($LASTEXITCODE -ne 0) { throw "Touch bridge tests failed: $LASTEXITCODE" }
$Alpha3Exe = Join-Path $Output 'touch_alpha3_test.exe'
& $Compiler "-B$CompilerBin/" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -DTOUCH_HEADLESS "-I$Touch" `
    @Core (Join-Path $Touch 'tests/touch_alpha3_test.c') -lm -o $Alpha3Exe
if ($LASTEXITCODE -ne 0) { throw "Touch Alpha 3 compilation failed: $LASTEXITCODE" }
& $Alpha3Exe (Join-Path $Output 'settings-test.cfg')
if ($LASTEXITCODE -ne 0) { throw "Touch Alpha 3 tests failed: $LASTEXITCODE" }
$Alpha4Exe = Join-Path $Output 'touch_alpha4_test.exe'
& $Compiler "-B$CompilerBin/" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -DTOUCH_HEADLESS "-I$Touch" `
    @Core (Join-Path $Touch 'tests/touch_alpha4_test.c') -lm -o $Alpha4Exe
if ($LASTEXITCODE -ne 0) { throw "Touch Alpha 4 compilation failed: $LASTEXITCODE" }
& $Alpha4Exe (Join-Path $Output 'settings-alpha4.cfg')
if ($LASTEXITCODE -ne 0) { throw "Touch Alpha 4 tests failed: $LASTEXITCODE" }
$AmmoExe = Join-Path $Output 'touch_ammo_test.exe'
& $Compiler "-B$CompilerBin/" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -DTOUCH_HEADLESS "-I$Touch" `
    @Core (Join-Path $Touch 'tests/touch_ammo_test.c') -lm -o $AmmoExe
if ($LASTEXITCODE -ne 0) { throw "Touch ammo compilation failed: $LASTEXITCODE" }
& $AmmoExe (Join-Path $Output 'settings-ammo.cfg')
if ($LASTEXITCODE -ne 0) { throw "Touch ammo tests failed: $LASTEXITCODE" }
$TargetExe = Join-Path $Output 'touch_target_test.exe'
& $Compiler "-B$CompilerBin/" -std=c11 -Wall -Wextra -Werror -pedantic -O2 `
    "-I$(Join-Path $Touch 'tests/stubs')" "-I$Touch" `
    (Join-Path $Touch 'tests/touch_target_test.c') -o $TargetExe
if ($LASTEXITCODE -ne 0) { throw "Touch target test compilation failed: $LASTEXITCODE" }
& $TargetExe
if ($LASTEXITCODE -ne 0) { throw "Touch target tests failed: $LASTEXITCODE" }

# Compile the real SDL queue adapter in one TU with explicit platform doubles.
# Compile the core separately so engine precompiled headers do not leak into it.
$Includes = @(
    "-I$Root", "-I$(Join-Path $Root '.deps/SDL2/include')", "-I$Touch",
    "-I$(Join-Path $Root 'SourceSnapshot/src/GameSrc/Headers')",
    "-I$(Join-Path $Root 'SourceSnapshot/src/MacSrc')",
    "-I$(Join-Path $Root 'SourceSnapshot/src/Libraries/H')",
    "-I$(Join-Path $Root 'SourceSnapshot/src/Libraries/LG/Source/LOG/src')"
)
$Includes += Get-ChildItem (Join-Path $Root 'SourceSnapshot/src/Libraries') -Directory | ForEach-Object {
    "-I$(Join-Path $_.FullName 'Source')"
}
$InputExe = Join-Path $Output 'touch_input_test.exe'
$CoreObjects = foreach ($Source in $Core) {
    $Object = Join-Path $Output (([IO.Path]::GetFileNameWithoutExtension($Source)) + '.o')
    & $Compiler "-B$CompilerBin/" -std=c11 -O2 -DTOUCH_HEADLESS "-I$Touch" -c $Source -o $Object
    if ($LASTEXITCODE -ne 0) { throw "Core test object compilation failed: $LASTEXITCODE" }
    $Object
}
& $Compiler "-B$CompilerBin/" -std=gnu11 -O2 -D__ANDROID__ -DDECLSPEC= -DSDL_MAIN_HANDLED -DTOUCH_HEADLESS `
    @Includes -include precompiled.h (Join-Path $Touch 'tests/touch_input_test.c') @CoreObjects -lm -o $InputExe
if ($LASTEXITCODE -ne 0) { throw "Touch input test compilation failed: $LASTEXITCODE" }
& $InputExe
if ($LASTEXITCODE -ne 0) { throw "Touch input tests failed: $LASTEXITCODE" }
$MenuExe = Join-Path $Output 'touch_menu_test.exe'
& $Compiler "-B$CompilerBin/" -std=gnu11 -O2 -D__ANDROID__ -DDECLSPEC= -DSDL_MAIN_HANDLED -DTOUCH_HEADLESS `
    @Includes -include precompiled.h (Join-Path $Touch 'tests/touch_menu_test.c') @CoreObjects -lm -o $MenuExe
if ($LASTEXITCODE -ne 0) { throw "Touch menu test compilation failed: $LASTEXITCODE" }
& $MenuExe
if ($LASTEXITCODE -ne 0) { throw "Touch menu tests failed: $LASTEXITCODE" }

$RenderExe = Join-Path $Output 'touch_render_test.exe'
& $Compiler "-B$CompilerBin/" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -DDECLSPEC= -DSDL_MAIN_HANDLED `
    "-I$(Join-Path $Root '.deps/SDL2/include')" "-I$Touch" `
    @Core (Join-Path $Touch 'touch_render.c') (Join-Path $Touch 'tests/touch_render_test.c') -lm -o $RenderExe
if ($LASTEXITCODE -ne 0) { throw "Touch render test compilation failed: $LASTEXITCODE" }
& $RenderExe
if ($LASTEXITCODE -ne 0) { throw "Touch render tests failed: $LASTEXITCODE" }
