"""Install user-owned MP3s and/or select a soundtrack in the debuggable QA app.

No downloads, data resets, or automatic app interruption. Python 3.9+ and adb.
Example: python scripts/install-qa-soundtrack.py C:/MyMusic --soundtrack remake
"""
import argparse
import datetime
import os
from pathlib import Path
import re
import subprocess
import tempfile
import uuid

PACKAGE = 'io.github.raposomiguel50.systemshock.qa'
FILENAMES = tuple(name + '.mp3' for name in (
    'medical', 'research', 'reactor', 'maintenance', 'storage', 'flightdeck',
    'executive', 'groves', 'engineering', 'security', 'bridge'))


def update_preferences(raw, mode):
    # Decode strictly: never silently replace unrelated preference bytes.
    text = raw.decode('utf-8-sig')
    lines = [line for line in text.splitlines(keepends=True)
             if not re.match(r'^\s*soundtrack\s*=', line, re.I)]
    text = ''.join(lines)
    if text and not text.endswith(('\n', '\r')):
        text += '\n'
    return (text + 'soundtrack = ' + mode + '\n').encode('utf-8')


class Device:
    def __init__(self, adb, serial):
        self.command = [adb] + (['-s', serial] if serial else [])

    def run(self, *args, **kwargs):
        return subprocess.run(self.command + list(args), check=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs).stdout

    def shell(self, command, **kwargs):
        return self.run('shell', '-T', 'run-as ' + PACKAGE + " sh -c '" + command + "'", **kwargs)

    def read(self, path):
        # exec-out also prevents the shell transport from rewriting CR/LF.
        return self.run('exec-out', 'run-as', PACKAGE, 'cat', path)

    def put(self, destination, source=None, data=None):
        # All destination names are internal constants, never user shell input.
        temporary = destination + '.audio2-tmp'
        # Windows adb can interpret stdin as text and truncate binary audio at
        # 0x1a, even through a Python PIPE. Use adb's binary sync protocol, then
        # copy the unique shell staging file into private app storage.
        staging = '/data/local/tmp/systemshock-audio2-' + uuid.uuid4().hex
        with tempfile.TemporaryDirectory(prefix='systemshock-audio2-') as scratch:
            if source is None:
                source = Path(scratch) / 'prefs.txt'
                source.write_bytes(data)
            size = source.stat().st_size
            try:
                self.run('push', str(source), staging)
                self.shell('cp ' + staging + ' ' + temporary)
            finally:
                self.run('shell', 'rm -f ' + staging)
        copied = int(self.shell('wc -c < ' + temporary).strip())
        if copied != size:
            raise RuntimeError(f'Transfer mismatch: {destination}: {copied} != {size}; original preserved')
        if data is not None and self.read(temporary) != data:
            raise RuntimeError('Preference verification failed; original preserved')
        self.shell('mv ' + temporary + ' ' + destination)
        if int(self.shell('wc -c < ' + destination).strip()) != size:
            raise RuntimeError('Final size verification failed: ' + destination)
        print(f'VERIFIED {destination}: {size} bytes')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', nargs='?', type=Path, help='Local folder of user-owned MP3s')
    parser.add_argument('--soundtrack', choices=('original', 'remake'))
    parser.add_argument('--serial', default='')
    sdk_adb = Path(os.environ.get('LOCALAPPDATA', '')) / 'Android/Sdk/platform-tools/adb.exe'
    parser.add_argument('--adb', default=str(sdk_adb) if sdk_adb.is_file() else 'adb')
    args = parser.parse_args()
    if args.source is None and args.soundtrack is None:
        parser.error('Supply a local source directory and/or --soundtrack.')
    recognized = {}
    if args.source is not None:
        if not args.source.is_dir():
            parser.error('Source must be an existing directory.')
        for source in args.source.iterdir():
            name = source.name.lower()
            if source.is_file() and name in FILENAMES:
                if name in recognized:
                    parser.error('Ambiguous filename: ' + name)
                if source.stat().st_size == 0:
                    parser.error('Empty MP3: ' + str(source))
                recognized[name] = source
        if not recognized:
            parser.error('No recognized filenames found. Expected: ' + ', '.join(FILENAMES))
    device = Device(args.adb, args.serial)
    device.run('get-state')
    # pidof returns nonzero when absent; normalize it on the device.
    if device.run('shell', 'pidof ' + PACKAGE + ' || true').strip():
        raise RuntimeError('Save your game and stop the QA app first; no data has been changed.')
    # Check prefs BEFORE copying tracks so setup errors are non-mutating.
    prefs = updated = None
    if args.soundtrack:
        prefs = device.read('files/prefs.txt')
        updated = update_preferences(prefs, args.soundtrack)
    if recognized:
        device.shell('mkdir -p files/music/remake')
        for name in FILENAMES:
            if name in recognized:
                device.put('files/music/remake/' + name, source=recognized[name])
        print('Not supplied (existing files left alone): ' + ', '.join(n for n in FILENAMES if n not in recognized))
    if updated is not None:
        backup = Path(__file__).resolve().parents[1] / 'dist/audio2/preferences'
        backup.mkdir(parents=True, exist_ok=True)
        backup_file = backup / (datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.txt')
        backup_file.write_bytes(prefs)
        device.put('files/prefs.txt', data=updated)
        print('Original preferences: ' + str(backup_file))
        print('Soundtrack: ' + args.soundtrack + ' (UTF-8 without BOM)')
    print('Start the QA app. No other files or app data were changed.')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        detail = getattr(error, 'stderr', b'')
        raise SystemExit(str(error) + ('\n' + detail.decode('utf-8', errors='replace') if detail else ''))
