"""Summarize existing VIS1 captures/timings and draw the inspected tile mask.
Uses Pillow; no game assets are included in the repository. Run after device QA.
"""
from pathlib import Path
import re
import statistics
from PIL import Image, ImageChops, ImageDraw

root=Path(__file__).resolve().parents[1]
out=root/'dist/vis1'
report=[]
def emit(*values):
    line=' '.join(map(str,values)); print(line); report.append(line)

log=(out/'final-device.log').read_text(encoding='utf-8-sig')
for mode,start,end in [(0,'01:52:00','01:52:59'),(1,'01:54:00','01:54:59')]:
    lines=[l for l in log.splitlines() if start<=l[6:14]<=end and f'mode={mode}' in l]
    emit('mode',mode,'device local time window',start,end)
    for field in ['world_mean_ms','world_max_ms','conversion_upload_ms','present_ms',
                  'interval_ms','max_interval_ms','over20ms']:
        values=[float(m.group(1)) for l in lines
                if (m:=re.search(r'\b'+field+r'=([\d.]+)',l))]
        if values:
            emit(field,'300-sample windows',len(values),'mean',round(statistics.mean(values),4),
                 'max',max(values),'sum',round(sum(values),4))

for first,second,regions in [
    ('full-enhanced','full-enhanced-repeat',{'world':(550,130,1750,800)}),
    ('B-original-final','B-original-repeat',{'world':(575,140,1780,710)}),
    ('outside-original','outside-enhanced',{'outside world':(550,130,1750,800)}),
    ('B-enhanced-clean','B-original-repeat',{
        'emissive upper panel':(1300,170,1650,260),
        'framed left MFD':(480,760,810,1060),
        'framed right MFD':(1520,760,1850,1060),
        'framed body/status HUD':(1090,0,1870,115)}),
    ('current','timing-enhanced-start',{'touch pause/set on black gutter':(0,0,445,130)})]:
    a=Image.open(out/(first+'.png')).convert('RGB')
    b=Image.open(out/(second+'.png')).convert('RGB')
    for label,box in regions.items():
        delta=ImageChops.difference(a.crop(box),b.crop(box))
        emit(first,second,label,'identical=',delta.getbbox() is None)

# Draw coordinates from the implementation's inspected data, not a sketched map.
source=(root/'SourceSnapshot/src/GameSrc/horror_profile.c').read_text()
tiles=re.findall(r'\{(\d+),(\d+),\d+,\d+,\d+,\d+,\d+,\d+,HV_(\w+)\}',source)
im=Image.new('RGB',(850,580),'#111820');draw=ImageDraw.Draw(im)
draw.text((25,20),'MEDICAL: medical-healing-suite-v1 / 17 inspected tiles',fill='white')
colors={'WARM':'#906444','COOL':'#326b80','ORIGINAL':'#59646c'}
for x in range(25,32): draw.text((95+(x-25)*100,60),str(x),fill='white')
for y in range(19,26): draw.text((32,103+(y-19)*60),str(y),fill='white')
for xs,ys,bank in tiles:
    x,y=int(xs),int(ys);px,py=65+(x-25)*100,80+(y-19)*60
    draw.rectangle((px,py,px+96,py+56),fill=colors[bank])
    draw.text((px+10,py+10),f'{x},{y}',fill='white')
draw.text((575,288),'START',fill='white')
draw.text((25,520),'Brown: warm alcove   Blue: cool bay/ramp   Gray: neutral landing',fill='white')
draw.text((25,545),'Outside mask: original. Grid orientation is map coordinates, not screen directions.',fill='white')
im.save(out/'region-mask.png')
(out/'evidence-summary.txt').write_text('\n'.join(report)+'\n')
