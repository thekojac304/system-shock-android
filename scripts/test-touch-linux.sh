#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
touch="$root/AndroidProject/app/jni/touch"
out="$root/dist/touch-tests-linux"
mkdir -p "$out"
core=("$touch/touch_controls.c" "$touch/touch_settings.c" "$touch/touch_editor.c" "$touch/touch_radial.c")
common=(-std=c11 -Wall -Wextra -Werror -pedantic -O2 -DTOUCH_HEADLESS "-I$touch")
for suite in touch_controls touch_alpha11 touch_alpha2 touch_alpha3 touch_alpha4 touch_qa touch_ammo; do
    gcc "${common[@]}" "${core[@]}" "$touch/tests/${suite}_test.c" -lm -o "$out/${suite}_test"
    if [[ "$suite" == touch_alpha3 || "$suite" == touch_alpha4 || "$suite" == touch_ammo ]]; then
        "$out/${suite}_test" "$out/${suite}-settings.cfg"
    else
        "$out/${suite}_test"
    fi
done
gcc "${common[@]}" -DDECLSPEC= -DSDL_MAIN_HANDLED \
    "-I$root/.deps/SDL2/include" "$touch/tests/touch_surface_test.c" -o "$out/touch_surface_test"
"$out/touch_surface_test"
gcc "${common[@]}" "-I$touch/tests/stubs" "${core[@]}" \
    "$touch/tests/touch_bridge_test.c" -lm -o "$out/touch_bridge_test"
"$out/touch_bridge_test"
gcc -std=c11 -Wall -Wextra -Werror -pedantic -O2 "-I$touch/tests/stubs" "-I$touch" \
    "$touch/tests/touch_target_test.c" -o "$out/touch_target_test"
"$out/touch_target_test"
includes=("-I$root" "-I$root/.deps/SDL2/include" "-I$touch"
    "-I$root/SourceSnapshot/src/GameSrc/Headers" "-I$root/SourceSnapshot/src/MacSrc"
    "-I$root/SourceSnapshot/src/Libraries/H"
    "-I$root/SourceSnapshot/src/Libraries/LG/Source/LOG/src")
for dir in "$root"/SourceSnapshot/src/Libraries/*/; do includes+=("-I${dir}Source"); done
objects=()
for src in "${core[@]}"; do
    obj="$out/$(basename "${src%.c}").o"
    gcc -std=c11 -O2 -DTOUCH_HEADLESS "-I$touch" -c "$src" -o "$obj"
    objects+=("$obj")
done
gcc -std=gnu11 -O2 -D__ANDROID__ -DDECLSPEC= -DSDL_MAIN_HANDLED -DTOUCH_HEADLESS \
    "${includes[@]}" -include precompiled.h "$touch/tests/touch_input_test.c" \
    "${objects[@]}" -lm -o "$out/touch_input_test"
"$out/touch_input_test"
gcc -std=gnu11 -O2 -D__ANDROID__ -DDECLSPEC= -DSDL_MAIN_HANDLED -DTOUCH_HEADLESS \
    "${includes[@]}" -include precompiled.h "$touch/tests/touch_menu_test.c" \
    "${objects[@]}" -lm -o "$out/touch_menu_test"
"$out/touch_menu_test"
gcc -std=c11 -Wall -Wextra -Werror -pedantic -O2 -DDECLSPEC= -DSDL_MAIN_HANDLED \
    "-I$root/.deps/SDL2/include" "-I$touch" "${core[@]}" "$touch/touch_render.c" \
    "$touch/tests/touch_render_test.c" -lm -o "$out/touch_render_test"
"$out/touch_render_test"
echo 'TOUCH_SUITES=PASS'
