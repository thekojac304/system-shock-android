"""Small explicit device QA commands. Requires an already imported QA installation.
Backups/captures stay in ignored dist/vis1; no proprietary data is bundled.
"""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', default='dist/vis1', help='Ignored QA evidence directory')
parser.add_argument('action', choices=['backup', 'capture', 'tap', 'key', 'swipe', 'log', 'install', 'launch'])
parser.add_argument('args', nargs='*')
options = parser.parse_args()
adb = str(Path(os.environ['LOCALAPPDATA']) / 'Android/Sdk/platform-tools/adb.exe')
package = 'io.github.raposomiguel50.systemshock.qa'
out = Path(options.out)
out.mkdir(parents=True, exist_ok=True)


def run(*args):
    return subprocess.run([adb, *args], check=True, capture_output=True).stdout


if options.action == 'backup':
    backup = out / 'before'
    backup.mkdir(exist_ok=False)
    files=run('shell','run-as',package,'ls','files').decode().splitlines()
    for name in ['prefs.txt', 'CurrentGame.dat', 'touch-settings.cfg'] + sorted(n for n in files if n.startswith('savgam') and n.endswith('.dat')):
        (backup / name).write_bytes(run('exec-out', 'run-as', package, 'cat', 'files/' + name))
    apk = run('shell', 'pm', 'path', package).decode().strip().removeprefix('package:')
    run('pull', apk, str(backup / 'installed.apk'))
    (backup / 'screen.png').write_bytes(run('exec-out', 'screencap', '-p'))
    print('Backed up QA saves, preferences, installed APK and screen.')
elif options.action == 'capture':
    path = out / (options.args[0] + '.png')
    path.write_bytes(run('exec-out', 'screencap', '-p'))
    print(path)
elif options.action in ('tap', 'key', 'swipe'):
    command = {'tap': 'tap', 'key': 'keyevent', 'swipe': 'swipe'}[options.action]
    run('shell', 'input', command, *options.args)
elif options.action == 'log':
    data = run('logcat', '-d', '-s', 'SDL/APP', 'libc', 'DEBUG', 'AndroidRuntime').decode(errors='replace')
    (out / 'device.log').write_text(data, encoding='utf-8')
    print('\n'.join(line for line in data.splitlines() if 'VIS1' in line or 'Fatal signal' in line))
elif options.action == 'install':
    print(run('install', '-r', 'AndroidProject/app/build/outputs/apk/qa/app-qa.apk').decode())
elif options.action == 'launch':
    print(run('shell', 'monkey', '-p', package, '1').decode())
