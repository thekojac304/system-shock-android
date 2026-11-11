"""Read-only VIS1B evidence analysis; does not alter any screenshot or game data.
Use final direct ADB captures and the bank dump produced by test-vis1.py.
"""
from pathlib import Path
import json
import re
import statistics
from PIL import Image, ImageChops

out=Path('dist/vis1b')
report=[]
def emit(*args):
    line=' '.join(map(str,args)); print(line); report.append(line)

if (out/'timing-windows.json').exists():
    logs=(out/'timing.log').read_text(encoding='utf-8').splitlines()
    for window in json.loads((out/'timing-windows.json').read_text()):
        lines=[l for l in logs if l.split() and l.split()[0].replace('.','',1).isdigit()
               and window['start']<=float(l.split()[0])<=window['end']
               and f"mode={window['mode']} " in l]
        emit('timing window',window)
        for field in ['world_mean_ms','world_max_ms','conversion_upload_ms','present_ms',
                      'interval_ms','max_interval_ms','over20ms']:
            values=[float(m.group(1)) for l in lines if (m:=re.search(r'\b'+field+r'=([\d.]+)',l))]
            if values: emit(field,'windows',len(values),'mean',round(statistics.mean(values),4),'max',max(values),'sum',sum(values))

banks=(out/'banks.bin').read_bytes()
for bank,name in enumerate(['Original','Conservative cool','Conservative warm','Horror cool','Horror warm']):
    data=banks[bank*4096:(bank+1)*4096]
    emit(name,'unique outputs by row:',[len(set(data[r*256:(r+1)*256])) for r in range(16)])
    emit(name,'changed entries:',sum(a!=b for a,b in zip(data,banks[:4096])))
pal=(Path('dist/vis1')/'palette.bin').read_bytes()
for c in [32,48,64,80,96,112,128,144,160,176,192,208,224,240,254]:
    emit('source',c,'RGB',list(pal[c*3:c*3+3]),'outputs at row 6 original/cool/warm',
         [(k:=banks[b*4096+6*256+c],list(pal[k*3:k*3+3])) for b in [0,3,4]])

for scene in ['A','B']:
    paths=[out/f'{scene}-{mode}.png' for mode in ['original','conservative','horror']]
    if not all(p.exists() for p in paths): continue
    imgs=[Image.open(p).convert('RGB') for p in paths]
    # Framed HUD world rectangle; menu/HUD/gutters excluded. Luma is screenshot
    # code-value evidence at fixed gamma, not calibrated luminance or aesthetics.
    box=(580,150,1770,700)
    for path,im in zip(paths,imgs):
        pixels=list(im.crop(box).getdata())
        y=[(2*r+4*g+b)/7 for r,g,b in pixels]
        emit(path.name,'world mean code luma',round(statistics.mean(y),2),
             'fraction below 12',round(sum(v<12 for v in y)/len(y),4))
    for label,area in [('left MFD',(480,760,810,1060)),('right MFD',(1520,760,1850,1060)),
                       ('body/status',(1090,0,1800,110))]:
        emit(scene,label,'Original/Horror identical',ImageChops.difference(imgs[0].crop(area),imgs[2].crop(area)).getbbox() is None)
for mode in ['original','horror']:
    a=out/f'B-{mode}.png'; b=out/f'B-{mode}-repeat.png'
    if a.exists() and b.exists():
        box=(580,150,1770,700)
        emit('B',mode,'repeat world identical',ImageChops.difference(
            Image.open(a).crop(box),Image.open(b).crop(box)).getbbox() is None)
if (out/'B-original.png').exists():
    box=(920,278,1170,310) # Interior of upper original blue fixture, same camera.
    emit('B upper fixture Original/Horror identical',ImageChops.difference(
        Image.open(out/'B-original.png').crop(box),Image.open(out/'B-horror.png').crop(box)).getbbox() is None)
(out/'evidence-summary.txt').write_text('\n'.join(report)+'\n',encoding='utf-8')
