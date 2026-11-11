"""Run pure flicker and level-preset checks on the connected Android QA phone."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
sdk = Path(os.environ['LOCALAPPDATA']) / 'Android/Sdk'
clang = sdk / 'ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
adb = sdk / 'platform-tools/adb.exe'
binary = root / 'dist/horror-hybrid1/horror-hybrid-test'
binary.parent.mkdir(parents=True, exist_ok=True)
subprocess.run([str(clang), '--target=aarch64-linux-android33', '-std=c11',
                '-Wall', '-Wextra', '-Werror', '-O2',
                '-I' + str(root / 'SourceSnapshot/src/MacSrc'),
                str(root / 'tests/visuals/horror_hybrid_test.c'),
                '-Wl,-z,max-page-size=16384', '-lm', '-o', str(binary)], check=True)
remote = '/data/local/tmp/shock-horror-hybrid-test'
try:
    subprocess.run([str(adb), 'push', str(binary), remote], check=True, capture_output=True)
    subprocess.run([str(adb), 'shell', 'chmod', '700', remote], check=True)
    result = subprocess.run([str(adb), 'shell', remote], check=True, capture_output=True, text=True)
    print(result.stdout.strip())
finally:
    subprocess.run([str(adb), 'shell', 'rm', '-f', remote], check=True)
