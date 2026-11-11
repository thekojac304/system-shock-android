"""Compile VIS1 production code with Android NDK; run bounded tests on ADB.
Run vis1-texture-probe.py first for the owned-data palette inputs (ignored dist).
No app save, game resource or preference is modified by this test.
"""
import os
from pathlib import Path
import subprocess

root=Path(__file__).resolve().parents[1]
sdk=Path(os.environ['LOCALAPPDATA'])/'Android/Sdk'
compiler=sdk/'ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
adb=sdk/'platform-tools/adb.exe'
out=root/'dist/vis1/tests';out.mkdir(parents=True,exist_ok=True)
headers=root/'SourceSnapshot/src/GameSrc/Headers'
core=root/'SourceSnapshot/src/GameSrc/horror_profile.c'
adapter=(root/'SourceSnapshot/src/GameSrc/horror_visuals.c').read_text()
adapter='\n'.join(line for line in adapter.splitlines() if not line.startswith('#include'))
fixture=(root/'tests/visuals/adapter_fixture.h').read_text()
checks=(root/'tests/visuals/adapter_checks.c').read_text()
(out/'adapter_test.c').write_text(fixture+'\n'+adapter+'\n'+checks)
prefs=(root/'SourceSnapshot/src/MacSrc/Prefs.c').read_text()
constants='\n'.join(line for line in prefs.splitlines() if line.startswith('static const char *PREF_'))
functions=prefs[prefs.index('static char *trim('):prefs.index('//  Set the corresponding Shock globals')]
(out/'preferences_test.c').write_text((root/'tests/visuals/preferences_fixture.h').read_text()+
    '\n'+constants+'\n'+functions+(root/'tests/visuals/preferences_checks.c').read_text())
remote='/data/local/tmp/shock-vis1-tests'
subprocess.run([str(adb),'shell','mkdir','-p',remote],check=True)
for name,src,args in [
    ('profile',root/'tests/visuals/profile_test.c',[remote+'/shade.bin',remote+'/palette.bin']),
    ('adapter',out/'adapter_test.c',[]),
    ('preferences',out/'preferences_test.c',[])]:
    exe=out/name
    extra=[]
    if name=='preferences':
        extra=['-DRP5NP_QA_TOOLS','-I'+str(root/'SourceSnapshot/src/MacSrc'),
               str(root/'SourceSnapshot/src/MacSrc/ExternalMusic.c')]
    subprocess.run([str(compiler),'--target=aarch64-linux-android33','-std=c11','-Wall','-Wextra',
                    '-Werror','-O2','-I'+str(headers),str(core),str(src),*extra,'-o',str(exe)],check=True)
    subprocess.run([str(adb),'push',str(exe),remote+'/'+name],check=True)
    if name=='profile':
        for file in ['shade.bin','palette.bin']:
            subprocess.run([str(adb),'push',str(out.parent/file),remote+'/'+file],check=True)
    subprocess.run([str(adb),'shell','chmod','700',remote+'/'+name],check=True)
    subprocess.run([str(adb),'shell',remote+'/'+name,*args],check=True)
