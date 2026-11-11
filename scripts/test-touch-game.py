"""Compile production ammo/loadout routines with native boundaries doubled.
The ammunition logic comes unchanged from weapons.c, including stock reloads.
Run: python scripts/test-touch-game.py --compiler PATH_TO_GCC
"""
import argparse
import os
import re
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
args = argparse.ArgumentParser(description=__doc__)
args.add_argument('--compiler', default='gcc')
compiler = args.parse_args().compiler
out = root / 'dist/touch-game-tests'
out.mkdir(parents=True, exist_ok=True)
input_source = (root / 'SourceSnapshot/src/GameSrc/input.c').read_text()
weapons = (root / 'SourceSnapshot/src/GameSrc/weapons.c').read_text()
qa = (root / 'AndroidProject/app/jni/touch/qa_tools.c').read_text()
ammo = input_source[input_source.index('static void touch_radial_label('):input_source.index('int touch_game_radial_snapshot(')]
native = weapons[weapons.index('void get_available_ammo_type('):weapons.index('// cool_off_beam_weapons()')]
reloads = weapons[weapons.index('uchar reload_current_weapon(void) {'):weapons.index('uchar ready_to_draw_handart(void) {')]
loadout = qa[qa.index('static const int qa_loadout_weapons[]'):qa.index('static unsigned read32(')]
fixture = (root / 'tests/touch/game_adapter_test.c').read_text()
# Keep the lightweight boundary model pinned to real inventory capacities.
for header, names in [('gamesys.h', ['NUM_GRENADEZ', 'NUM_DRUGZ']),
                      ('player.h', ['NUM_WEAPON_SLOTS', 'PLAYER_MAX_HP'])]:
    text = (root / 'SourceSnapshot/src/GameSrc/Headers' / header).read_text()
    for name in names:
        actual = re.search(r'^#define\s+' + name + r'\s+(\d+)', text, re.M).group(1)
        fixture = re.sub(r'(#define\s+' + name + r'\s+)\d+', r'\g<1>' + actual, fixture)
source = out / 'game_adapter_test.c'
source.write_text(fixture.replace('/* PRODUCTION_ROUTINES */', native + reloads + ammo + loadout))
env = os.environ.copy()
compiler_dir = str(Path(compiler).resolve().parent)
env['PATH'] = compiler_dir + os.pathsep + env.get('PATH', '')
exe = out / 'game_adapter_test.exe'
subprocess.run([compiler, '-B' + compiler_dir + '/', '-std=c11', '-O2',
                '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                '-I' + str(root / 'AndroidProject/app/jni/touch'),
                '-I' + str(root / 'SourceSnapshot/src/GameSrc/Headers'),
                str(source), '-o', str(exe)], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
