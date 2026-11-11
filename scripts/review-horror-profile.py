"""Render offline maps and quantify generated profiles. Requires Pillow.

python scripts/review-horror-profile.py dist/horror-author1/generated/*.generated.json
    --output dist/horror-author1/review
The Medical hand-authored mask is used only here as an AFTER-generation holdout.
It never feeds the generator, segmentation, fixture catalog or archetype rules.
"""
import argparse
from collections import Counter
import glob
import json
from pathlib import Path
import re
from PIL import Image,ImageDraw
import horror_author as h

COLORS={'luminous_fixture':'#a6fbff','monitor_display':'#46bad0','clinical_panel':'#4876ae',
    'industrial_wall':'#b28a47','decorative':'#976675','damaged_surface':'#b73536',
    'organic_grove':'#389957','machinery':'#8b7eb9','unknown':'#494952'}
MODES=['Regions','Floor darkness','Ceiling darkness','Fixture candidates','Wall material','Hazard / state']


def holdout_map(p,reference,bounds=(24,18,33,27),scale=42):
    """Reference is loaded only by this review module, after generation."""
    x0,y0,x1,y1=bounds;pw=(x1-x0)*scale;ph=(y1-y0)*scale
    im=Image.new('RGB',(3*pw+80,ph+110),'#11151d');draw=ImageDraw.Draw(im)
    tiles={t['id']:t for t in p['diagnostics']['tiles']}
    rec=Counter(i for l in p['lights'] for i in l['receiver_tiles'])
    suppressed=Counter(c['tile'] for c in p['diagnostics']['candidates'] if c['policy'].startswith('suppressed'))
    portals=p['diagnostics'].get('door_portals',[])
    draw.text((20,14),'AUTHOR1C Medical Healing Suite | offline inference; white outlines = held-out reference tiles',fill='white')
    for panel,title in enumerate(('Inferred regions / portals','Generated receiver masks / sources','Suppressed fixtures / emissive review')):
        ox=20+panel*(pw+20);oy=58;draw.text((ox,38),title,fill='white')
        for y in range(y0,y1):
            for x in range(x0,x1):
                i=y*64+x;t=tiles.get(i)
                if not t:continue
                if panel==0:color='#'+h.digest(t['region'].encode())[:6]
                elif panel==1:color=(25+min(4,rec[i])*43,58,82) if rec[i] else '#2b3540'
                else:color='#b66145' if suppressed[i] else '#9c7945' if any(
                    c['tile']==i and c['classification']=='luminous_fixture' for c in p['diagnostics']['candidates']) else '#35444f'
                px=ox+(x-x0)*scale;py=oy+(y-y0)*scale
                draw.rectangle((px,py,px+scale-2,py+scale-2),fill=color)
                if i in reference:draw.rectangle((px+1,py+1,px+scale-3,py+scale-3),outline='white',width=2)
                draw.text((px+3,py+3),str(i),fill='#e6eef4')
        if panel==0:
            for portal in portals:
                x,y=portal['position'][:2]
                if x0<=x<x1 and y0<=y<y1:
                    px=ox+(x-x0)*scale;py=oy+(y-y0)*scale
                    draw.ellipse((px-5,py-5,px+5,py+5),fill='#ff3838')
        elif panel==1:
            for light in p['lights']:
                x,y=light['position'][:2]
                if x0<=x<x1 and y0<=y<y1:
                    px=ox+(x-x0)*scale;py=oy+(y-y0)*scale
                    draw.ellipse((px-5,py-5,px+5,py+5),fill='#fff173')
    draw.text((20,ph+70),'Red = closed portal; yellow = inferred light; amber = luminous material needing pixel mask; orange = suppressed candidate.',fill='#c4cbd5')
    return im


def maps(p,bounds=(0,0,64,64),scale=5):
    x0,y0,x1,y1=bounds;pw=(x1-x0)*scale;ph=(y1-y0)*scale
    width=pw+36;height=ph+80
    im=Image.new('RGB',(width*3,height*2+65),'#11151d');draw=ImageDraw.Draw(im)
    draw.text((18,12),f'HORROR-AUTHOR1 / Level {p["level_id"]} / OFFLINE DIAGNOSTIC, not rendered gameplay',fill='white')
    candidates=Counter(c['tile'] for c in p['diagnostics']['candidates'])
    for panel,mode in enumerate(MODES):
        ox=(panel%3)*width+18;oy=(panel//3)*height+60
        draw.text((ox,oy-23),mode,fill='white')
        for t in p['diagnostics']['tiles']:
            if not x0<=t['x']<x1 or not y0<=t['y']<y1:continue
            if panel==0:color='#'+h.digest(t['region'].encode())[:6]
            elif panel in (1,2):
                v=255-round(t['darkness'][panel-1]/15*240);color=(v,v,v)
            elif panel==3:color='#9ae7ff' if candidates[t['id']] else '#38414a'
            elif panel==4:color=COLORS[t['material_classes']['wall']]
            else:color='#c449b6' if t['hazards']['bio'] else '#cbd24d' if t['hazards']['radiation'] else '#be563e' if t['hazards']['peril'] else '#3986af' if t['music']==7 else '#38414a'
            x=ox+(t['x']-x0)*scale;y=oy+(t['y']-y0)*scale
            draw.rectangle((x,y,x+scale-1,y+scale-1),fill=color)
            if not t['rectangular'] or t['safe_slab'][0]>=t['safe_slab'][1]:
                draw.line((x,y,x+scale-1,y+scale-1),fill='#ffb871')
            if scale>=20:draw.text((x+2,y+2),str(t['id']),fill='white')
        for o in p['diagnostics']['objects']:
            x,y=o['position'][:2]
            if not x0<=x<x1 or not y0<=y<y1:continue
            if 'door' in o or (panel==5 and o.get('event',{}).get('type',0)):
                px=ox+(x-x0)*scale;py=oy+(y-y0)*scale
                draw.rectangle((px-1,py-1,px+1,py+1),fill='#ff4040' if 'door' in o else 'white')
        if panel==3:
            circles=Image.new('RGBA',(pw,ph),(0,0,0,0));circle_draw=ImageDraw.Draw(circles)
            for light in p['lights']:
                x,y=light['position'][:2]
                if not x0<=x<x1 or not y0<=y<y1:continue
                px=(x-x0)*scale;py=(y-y0)*scale;r=light['radius']*scale
                circle_draw.ellipse((px-r,py-r,px+r,py+r),outline='#bdb078')
            im.paste(circles,(ox,oy),circles)
        legends=['Colors: region IDs. Orange: uncertain geometry.','White: D=0; dark: D=15. Original values.',
            'Independent original ceiling darkness.', 'Cyan: candidates. Rings: radius, NOT receivers.',
            'Cyan: fixture; blue: clinical; red: damage.', 'Pink: bio; yellow: rad; blue: elevator.']
        draw.text((ox,oy+ph+10),legends[panel],fill='#c4cbd5')
        draw.text((ox,oy+ph+26),'Red squares: doors. No automatic fog enabled.',fill='#c4cbd5')
    return im


def summarize(p):
    tiles=p['diagnostics']['tiles'];ts={t['id']:t for t in tiles};objects=p['diagnostics']['objects']
    rec={i for light in p['lights'] for i in light['receiver_tiles']}
    multi={i for r in p['regions'] if len(r['tiles'])>1 for i in r['tiles']}
    result=dict(summary=p['diagnostics']['summary'],
        candidate_policies=dict(Counter(c['policy'] for c in p['diagnostics']['candidates'])),
        region_classes=dict(Counter(r['classification'] for r in p['regions'])),
        largest_region_tiles=max(len(r['tiles']) for r in p['regions']),
        tiles_in_multitile_regions=len(multi),
        receiver_coverage_percent=round(100*len(rec)/len(tiles),2),
        region_multitile_coverage_percent=round(100*len(multi)/len(tiles),2),
        original_darkness_histograms=[dict(sorted(Counter(t['darkness'][a] for t in tiles).items())) for a in (0,1)],
        dark_tiles_without_generated_light=sum(min(t['darkness'])>=10 and t['id'] not in rec for t in tiles),
        dark_tiles_total=sum(min(t['darkness'])>=10 for t in tiles),
        hazards={k:sum(t['hazards'][k] for t in tiles) for k in ('bio','radiation','peril')},
        music_zones=dict(sorted(Counter(t['music'] for t in tiles).items())),
        events=dict(Counter(e['kind'] for e in p['diagnostics']['events'])),
        unlit_object_count=sum(o.get('unlit',False) for o in objects),
        named_lamp_objects=[o['id'] for o in objects if o['obclass']==7 and o['subclass']==3],
        unique_materials=len(p['material_rules']),
        unknown_materials=[m['resource_id'] for m in p['material_rules'] if m['classification']=='unknown'])
    # Direct endpoint IDs only. qdata indirection and arbitrary comparators are
    # deliberately retained for the runtime resolver, not assigned story labels.
    by_id={o['id']:o for o in objects};decoded=[]
    for e in p['diagnostics']['events']:
        if e['kind']!='lighting':continue
        a,b,c,d=e['parameters'];first=a&65535;last=a>>16;mode=c&65535
        row=dict(object_id=e['object_id'],mode=mode,p2_state=b>>28,
            transition_steps=(b>>16)&4095,transition_type=b&65535,
            channels='both' if c&0x20000 and not c&0x10000 else 'ceiling' if c&0x10000 else 'floor',
            targets=list(d.to_bytes(4,'little')),comparator=e['comparator'],bounds=None)
        if not first&0x3000 and not last&0x3000:
            if mode==3:
                x,y=by_id[e['object_id']]['position'][:2]
                row['bounds']=[int(x)-first,int(y)-first,int(x)+first,int(y)+first]
            elif first in by_id and last in by_id:
                x,y=by_id[first]['position'][:2];u,v=by_id[last]['position'][:2]
                row['bounds']=[int(min(x,u)),int(min(y,v)),int(max(x,u)),int(max(y,v))]
        decoded.append(row)
    result['decoded_lighting_events']=decoded
    if p['level_id']==1:
        source=(h.ROOT/'SourceSnapshot/src/GameSrc/horror_profile.c').read_text().split('const HvTile hv_medical_tiles[] = {',1)[1].split('};',1)[0]
        reference={int(y)*64+int(x) for x,y in re.findall(r'\{(\d+),(\d+),',source)}
        result['medical_holdout']=dict(reference_tiles=sorted(reference),
            inferred_regions=sorted({ts[i]['region'] for i in reference}),
            luminous_candidates=[c['id'] for c in p['diagnostics']['candidates'] if c['tile'] in reference and c['classification']=='luminous_fixture'],
            generated_lights=[light['id'] for light in p['lights'] if any(i in reference for i in light['receiver_tiles'])],
            reference_tiles_receiving_light=sorted(reference&rec),
            receiver_face_audit=h.receiver_face_audit(p,reference),
            known_displays_detected={str(oid):any(c['source_object_id']==oid for c in p['diagnostics']['candidates']) for oid in (699,718)},
            note='Reference mask read AFTER generation only. Warm alcove and two proof-light positions are not generator inputs.')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('profiles',nargs='+')
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    paths=sorted({Path(p) for pattern in args.profiles for p in glob.glob(pattern)})
    if not paths:parser.error('No profiles found')
    args.output.mkdir(parents=True,exist_ok=True);report={}
    for path in paths:
        p=json.loads(path.read_text());h.validate(p);level=p['level_id'];report[level]=summarize(p)
        maps(p).save(args.output/f'level-{level}.overview.png')
        if level==1:
            maps(p,(24,18,33,27),36).save(args.output/'medical-holdout.png')
            holdout_map(p,set(report[level]['medical_holdout']['reference_tiles'])).save(args.output/'medical-holdout-author1c.png')
    (args.output/'review.json').write_bytes(h.encode(report))
    print(json.dumps({l:{k:v for k,v in r.items() if k in ('summary','receiver_coverage_percent','region_multitile_coverage_percent','medical_holdout')} for l,r in report.items()},indent=2))


if __name__=='__main__':main()
