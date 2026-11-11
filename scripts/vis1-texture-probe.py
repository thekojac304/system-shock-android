"""Local QA contact sheet from owned data; output remains ignored in dist/."""
import os
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw
import importlib.util
import sys

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('inspect_vis1', Path(__file__).with_name('inspect-vis1-map.py'))
inspect = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspect)
adb = str(Path(os.environ['LOCALAPPDATA'])/'Android/Sdk/platform-tools/adb.exe')
def read(name):
    return subprocess.run([adb,'exec-out','run-as','io.github.raposomiguel50.systemshock.qa',
                           'cat','files/res/data/'+name],check=True,capture_output=True).stdout
out=Path('dist/vis1'); out.mkdir(exist_ok=True,parents=True)
palres=inspect.resources(read('GAMEPAL.RES'),0,65536)
palette=next(v for v in palres.values() if len(v)==768)
(out/'palette.bin').write_bytes(palette)
(out/'shade.bin').write_bytes(read('SHADTABL.DAT'))
res=inspect.resources(read('TEXTURE.RES'),0,65536)
sheet=Image.new('RGB',(768,480),(25,25,25)); draw=ImageDraw.Draw(sheet)
for i,tid in enumerate([35,37,38,45,51,108,216,219]):
    data=res[1000+tid]
    pos=int.from_bytes(data[2:6],'little')
    frame=data[pos:]
    w=int.from_bytes(frame[8:10],'little'); h=int.from_bytes(frame[10:12],'little')
    print(tid, 'frame',frame[:28].hex(),w,h)
    img=Image.frombytes('P',(w,h),bytes(frame[28:28+w*h])); img.putpalette(palette)
    x=(i%4)*192;y=(i//4)*240
    sheet.paste(img.resize((192,192)),(x,y)); draw.text((x+5,y+198),str(tid),fill='white')
sheet.save(out/'textures.png')
