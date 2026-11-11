"""Build/run the isolated GPU-RENDER1 GLES3 capability probe on one ADB device.
No APK install, app launch, preferences, saves or proprietary resources touched.
Not a world-renderer test or benchmark. Output stays in ignored dist/gpu-render1.
"""
import hashlib
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
sdk = Path(os.environ['LOCALAPPDATA']) / 'Android/Sdk'
clang = sdk / 'ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
adb = sdk / 'platform-tools/adb.exe'
out = root / 'dist/gpu-render1'
out.mkdir(parents=True, exist_ok=True)


def run(args):
    p = subprocess.run([str(a) for a in args], text=True, capture_output=True)
    return p


devices = run([adb, 'devices'])
devices.check_returncode()
ready = [line.split()[0] for line in devices.stdout.splitlines()[1:]
         if len(line.split()) == 2 and line.split()[1] == 'device']
if len(ready) != 1:
    raise SystemExit('Connect exactly one authorized ADB device.')
target = [adb, '-s', ready[0]]
source = root / 'tests/rendering/gles3_probe.c'
binary = out / 'gles3-probe'
build = run([clang, '--target=aarch64-linux-android33', '-std=c11', '-Wall',
             '-Wextra', '-Werror', '-O2', source, '-lEGL', '-lGLESv3', '-lm',
             '-Wl,-z,max-page-size=16384', '-o', binary])
(out / 'build.log').write_text(build.stdout + build.stderr, encoding='utf-8')
if build.returncode:
    print(build.stdout + build.stderr)
    build.check_returncode()
remote = '/data/local/tmp/shock-gpu-render1-probe'
try:
    push = run([*target, 'push', binary, remote])
    push.check_returncode()
    run([*target, 'shell', 'chmod', '700', remote]).check_returncode()
    metadata = []
    for prop in ['ro.product.model', 'ro.build.version.release', 'ro.opengles.version']:
        p = run([*target, 'shell', 'getprop', prop])
        p.check_returncode()
        metadata.append(prop + '=' + p.stdout.strip())
    result = run([*target, 'shell', remote])
    evidence = '\n'.join(metadata) + '\n'
    evidence += 'source_sha256=' + hashlib.sha256(source.read_bytes()).hexdigest() + '\n'
    evidence += 'binary_sha256=' + hashlib.sha256(binary.read_bytes()).hexdigest() + '\n'
    evidence += result.stdout + result.stderr
    (out / 'capability-probe.log').write_text(evidence, encoding='utf-8')
    print(evidence)
    result.check_returncode()
finally:
    # Remove only this executable; never remove an app or a directory.
    cleanup = run([*target, 'shell', 'rm', '-f', remote])
    cleanup.check_returncode()
