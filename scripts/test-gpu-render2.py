"""Build/run isolated GPU cache and geometry tests on one authorized ADB device.

Only temporary /data/local/tmp executables are pushed, run and removed. No APK
install, app launch, app data, preferences, saves, security settings or original
game resources are accessed. These are unit tests, not renderer parity evidence.
Build/test logs and binaries stay in ignored dist/gpu-render2-3-4.
"""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'dist/gpu-render2-3-4'
TESTS = (
    ('cache', 'tests/rendering/gpu_texture_cache_test.c', (
        'SourceSnapshot/src/MacSrc/GpuTextureCache.c',
        'SourceSnapshot/src/MacSrc/GpuTextureCache.h',
    )),
    ('geometry', 'tests/rendering/gpu_geometry_test.c', (
        'SourceSnapshot/src/MacSrc/GpuGeometry.h',
    )),
)


def run(args):
    return subprocess.run([str(arg) for arg in args], text=True,
                          capture_output=True, timeout=60)


def check(result):
    if result.returncode:
        print(result.stdout + result.stderr, end='')
        result.check_returncode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test', choices=('cache', 'geometry'),
                        help='Run only this fixture; default runs both.')
    args = parser.parse_args()
    sdk = Path(os.environ['LOCALAPPDATA']) / 'Android/Sdk'
    clang = sdk / 'ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
    adb = sdk / 'platform-tools/adb.exe'
    devices = run([adb, 'devices'])
    check(devices)
    ready = [fields[0] for line in devices.stdout.splitlines()[1:]
             if len(fields := line.split()) == 2 and fields[1] == 'device']
    if len(ready) != 1:
        raise SystemExit('Connect exactly one authorized ADB device.')
    target = [adb, '-s', ready[0]]
    OUT.mkdir(parents=True, exist_ok=True)

    # Both fixtures include their production implementation directly. In
    # particular, the cache fixture overrides malloc before including the .c
    # file to test allocation failure. Do not add that .c as a second input.
    for name, source_name, dependencies in TESTS:
        if args.test and name != args.test:
            continue
        source = ROOT / source_name
        binary = OUT / f'gpu-{name}-test'
        build = run([clang, '--target=aarch64-linux-android33', '-std=c11',
                     '-Wall', '-Wextra', '-Werror', '-O2', source,
                     '-Wl,-z,max-page-size=16384', '-o', binary])
        (OUT / f'{name}-test-build.log').write_text(
            build.stdout + build.stderr, encoding='utf-8')
        check(build)

        log = OUT / f'{name}-test.log'
        evidence = ''.join(
            f'sha256 {relative}={hashlib.sha256((ROOT / relative).read_bytes()).hexdigest()}\n'
            for relative in (source_name, *dependencies)
        )
        evidence += f'binary_sha256={hashlib.sha256(binary.read_bytes()).hexdigest()}\n'
        log.write_text(evidence, encoding='utf-8')
        remote = f'/data/local/tmp/shock-gpu2-{name}-test'
        try:
            check(run([*target, 'push', binary, remote]))
            check(run([*target, 'shell', 'chmod', '700', remote]))
            result = run([*target, 'shell', remote])
            evidence += result.stdout + result.stderr
            evidence += f'exit_code={result.returncode}\n'
            log.write_text(evidence, encoding='utf-8')
            print(result.stdout + result.stderr, end='')
            result.check_returncode()
        finally:
            # Exact test path only: never remove an app, wildcard or directory.
            cleanup = run([*target, 'shell', 'rm', '-f', remote])
            with log.open('a', encoding='utf-8') as stream:
                stream.write(f'cleanup_exit_code={cleanup.returncode}\n')
                stream.write(cleanup.stdout + cleanup.stderr)
            check(cleanup)


if __name__ == '__main__':
    main()
