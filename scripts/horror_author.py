"""HORROR-AUTHOR1 offline inference. Reads owned archives; never edits game data.

python scripts/horror_author.py --archive ARCHIVE.DAT --textprop TEXTPROP.DAT
    --output dist/horror-author1/generated [--levels 1 6] [--overlay overrides.json]

Only Medical/Executive are enabled for this feasibility phase. JSON and SVG are
diagnostic proposals, not a runtime-loaded or visually accepted game profile.
"""
import argparse
from collections import Counter
import copy
import hashlib
import html
import json
import math
from pathlib import Path
import re
import runpy
import struct

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = runpy.run_path(str(ROOT / 'scripts/inspect-vis1-map.py'))['resources']
PROPS = runpy.run_path(str(ROOT / 'scripts/inspect-gpu-terrain.py'))['texture_properties']
RULES = ROOT / 'profiles/horror-author1/rules.json'
SCHEMA = json.loads((ROOT/'profiles/horror-author1/profile.schema.json').read_text())
VERSION = 1
DIRECTIONS = ((0,-1),(1,0),(0,1),(-1,0))
# The renderer's face_obstruct table is read from its source, not inferred from
# tile names. A bad or changed table fails closed instead of opening walls.
PT_OFFSETS = (0., .125, 19195/65536, .5, 1-19195/65536, .875, 1.)


def face_table():
    source=(ROOT/'SourceSnapshot/src/GameSrc/frtables.c').read_text()
    body=source.split('uchar face_obstruct[FRFACEOBSTRUCTCNT][FACE_CNT] = {',1)[1].split('\n};',1)[0]
    rows=[];named={'foNul':255,'foClr':6,'foLft':3,'foRgt':30}
    body='\n'.join(line.split('//',1)[0] for line in body.splitlines())
    for match in re.finditer(r'fo(?:N|C)\(\)|fo(?:fo|F|4)\([^)]*\)',body):
        line=match.group()
        if line=='foN()':row=[255]*4
        elif line=='foC()':row=[6]*4
        elif line.startswith('fofo('):row=[named[x.strip()] for x in line[5:-1].split(',')]
        elif line.startswith('foF('):
            bits=[int(x.strip()=='ZP') for x in line[4:-1].split(',')]
            row=[6|(a<<7)|(b<<6) for a,b in zip(bits[::2],bits[1::2])]
        elif line.startswith('fo4('):
            bits=[int(x.strip(),0) for x in line[4:-1].split(',')]
            row=[((a<<3)|b)&255 for a,b in zip(bits[::2],bits[1::2])]
        else:raise ValueError('Unknown face_obstruct row: '+line)
        if len(row)!=4:raise ValueError('Invalid face_obstruct row')
        rows.append(row)
    if len(rows)!=51:raise ValueError('Changed face_obstruct table')
    return rows


FACES=face_table()


def surface_height(t,face,x=.5,y=.5):
    """Representative plane height; for complex slopes use edge interpolation."""
    base=t[face];kind=t['type'];slope=t['param']/2**t.get('height_shift',3)
    if not 6<=kind<=21 or (face=='floor' and t['mirror']==3) or (face=='ceiling' and t['mirror']==2):return base
    # Basic slopes and corner slopes have corner heights encoded by FACES.
    corners=[]
    for edge,s in ((0,x),(2,1-x)):
        for q in (0.,1.):
            z=edge_surface(t,edge,face,q)
            if z is not None:corners.append(z)
    if len(corners)==4:return sum(corners)/4
    return base+slope/2*(1 if face=='floor' else -1)


def edge_surface(t,edge,face,s):
    code=FACES[t['type']][edge]
    if code==255:return None
    z=t[face];p=t['param']/2**t.get('height_shift',3)
    if not p:return z
    flip=False if face=='floor' else t['mirror'] in (0,3)
    enabled=(t['mirror']!=3 if face=='floor' else t['mirror']!=2)
    if not enabled:return z
    a=bool(code & 128) ^ bool(flip)
    b=bool(code & 64) ^ bool(flip)
    return z + (p if face=='floor' else -p)*((1-s)*a+s*b)


def aperture(t,u,edge,s=.5):
    """Shared visible edge interval at normalized edge position s."""
    a=FACES[t['type']][edge];b=FACES[u['type']][(edge+2)%4]
    if a==255 or b==255:return None
    left=max(PT_OFFSETS[(a>>3)&7],1-PT_OFFSETS[b&7])
    right=min(PT_OFFSETS[a&7],1-PT_OFFSETS[(b>>3)&7])
    if right-left<.04 or not left+.01<=s<=right-.01:return None
    zs=[edge_surface(t,edge,f,s) for f in ('floor','ceiling')]
    ws=[edge_surface(u,(edge+2)%4,f,1-s) for f in ('floor','ceiling')]
    low=max(zs[0],ws[0]);high=min(zs[1],ws[1])
    return [low,high] if high-low>.06 else None


def open_aperture(t,u,edge):
    return any(aperture(t,u,edge,s) is not None for s in (.125,.25,.375,.5,.625,.75,.875))
TRAPS = ['null','teleport','damage','create_object','questbit','cutscene','multi',
         'lighting','sfx','height','terrain','scheduler','alternating_splitter',
         'destroy_object','plot_clock','email','expose','instance','animate','hack',
         'texture','ai','bark','monster','transmogrify']
DISPLAY_TRIPLES = {0x70006,0x70007,0x70206,0x70208,0x70209}


def object_properties(path):
    """Version45 original file, common 27-byte records at EOF (objsim.c:1856).

    Type ordering comes from numtypes_array; unknown file sizes fail closed.
    Class-specific property records are not interpreted by this diagnostic.
    """
    raw=path.read_bytes()
    if len(raw)!=17951 or struct.unpack_from('<I',raw)[0]!=45:
        raise ValueError('Unsupported OBJPROP.DAT layout; expected original v45/17951 bytes')
    headers='\n'.join(p.read_text() for p in sorted((ROOT/'SourceSnapshot/src/GameSrc/Headers').glob('obj*.h')))
    constants={k:int(v) for k,v in re.findall(r'^#define\s+(NUM_\w+)\s+(\d+)\s*$',headers,re.M)}
    source=(ROOT/'SourceSnapshot/src/GameSrc/objprop.c').read_text()
    body=source.split('static uchar numtypes_array',1)[1].split('};',1)[0].split('= {',1)[1]
    groups=[re.findall(r'NUM_\w+',row) for row in re.findall(r'\{([^}]+)\}',body)]
    triples=[(cl<<16)|(sub<<8)|typ for cl,rows in enumerate(groups)
             for sub,name in enumerate(rows) for typ in range(constants[name])]
    if len(groups)!=15 or len(triples)!=476:raise ValueError('Changed object type layout')
    start=len(raw)-len(triples)*27;result={}
    for i,triple in enumerate(triples):
        record=raw[start+27*i:start+27*(i+1)]
        result[triple]=dict(render_type=record[7],flags=struct.unpack_from('<H',record,20)[0])
    return result


def emission_policy(rules,kind,tid,face):
    policy=rules['surface_emission']
    if kind=='monitor_display':return policy['display']
    if kind!='luminous_fixture':return 'review_only'
    if face=='ceiling':return policy['luminous_ceiling']
    if face=='floor' and tid in policy['low_floor_ids']:return 'emits_low'
    if face=='wall' and tid in policy['low_wall_ids']:return 'emits_low'
    return 'review_only'


def digest(value):
    return hashlib.sha256(value).hexdigest()


def encode(value):
    return (json.dumps(value, indent=2, sort_keys=True, allow_nan=False)+'\n').encode()


def load_level(res, level, props):
    base = 4000 + 100*level
    info, raw = res[base+4], res[base+5]
    version = int.from_bytes(res[base+2], 'little')
    if len(info) != 58 or version not in (11,12):
        raise ValueError('Unsupported FullMap layout/version')
    w,h = struct.unpack_from('<II', info)
    zshift = struct.unpack_from('<I',info,16)[0]
    if (w,h)!=(64,64) or info[24] or not 0<=zshift<=5 or len(raw)!=w*h*16:
        raise ValueError('Unsupported dimensions/cyberspace/heights/tile length')
    ids = [v[0] for v in struct.iter_unpack('<H',res[base+7])]
    tiles = {}
    for i in range(w*h):
        t=raw[i*16:i*16+16]
        if not t[0]:
            continue
        if t[0]>50:
            raise ValueError('Unknown terrain type')
        packed=int.from_bytes(t[6:8],'little')
        slots={'wall':packed&63,'floor':packed>>11,'ceiling':(packed>>6)&31}
        textures={}
        for face,s in slots.items():
            if s>=len(ids):
                # A friend-material slope draws each external wall with its
                # neighbor's slot and has no internal wall of its own. Its
                # stored wall slot is unused, so keep it absent rather than
                # inventing a resource ID for an out-of-range table entry.
                if face=='wall' and t[9]&1 and 6<=t[0]<=17:
                    textures[face]=None
                    continue
                raise ValueError('Unresolved texture slot')
            if ids[s]>=len(props):
                raise ValueError('Unresolved texture slot')
            textures[face]=ids[s]
        tiles[i]=dict(id=i,x=i%w,y=i//w,type=t[0],height_shift=zshift,floor=(t[1]&31)/2**zshift,
            ceiling=(32-(t[2]&31))/2**zshift,param=t[3],mirror=(t[9]>>2)&3,
            textures=textures,
            neighbor_material=bool(t[9]&1),family_bit=bool(t[8]&128),
            darkness=[t[10]&15,t[11]&15],transient=[t[15]&15,t[15]>>4],
            fullbright=(t[11]>>4)&7,music=t[9]>>5,
            hazards={'bio':bool(t[1]&128),'radiation':bool(t[2]&128),'peril':bool(t[9]&16)},
            flicker=t[14]>>4)
        tile=tiles[i]
        slope=tile['param']/2**zshift if 6<=tile['type']<=21 else 0
        tile['safe_slab']=[tile['floor']+(slope if tile['mirror']!=3 else 0),
                           tile['ceiling']-(slope if tile['mirror']!=2 else 0)]
        tile['rectangular']=tile['type']==1 or 6<=tile['type']<=21
    objects=[]
    data=res[base+8];pad=int(version==12);size=27+pad
    if len(data)%size:
        raise ValueError('Bad object array')
    for oid in range(1,len(data)//size):
        o=data[oid*size:(oid+1)*size]
        if not o[0]:
            continue
        x,y=struct.unpack_from('<HH',o,11+pad)
        obj=dict(id=oid,obclass=o[1],subclass=o[2],type=o[20+pad],
            spec_id=struct.unpack_from('<H',o,3+pad)[0],
            position=[x/256,y/256,o[15+pad]/2**(3+zshift)],
            tile=(y//256)*w+x//256,pitch=o[16+pad],heading=o[17+pad],bank=o[18+pad],
            hp=struct.unpack_from('<h',o,21+pad)[0],frame=o[24+pad],flags=o[26+pad])
        obj['triple']=(obj['obclass']<<16)|(obj['subclass']<<8)|obj['type']
        if obj['obclass'] in (9,10,12):
            offset,n={9:(19,30),10:(20,14),12:(22,28)}[obj['obclass']]
            specs=res[base+offset];sid=obj['spec_id']
            if len(specs)%n or sid*n+n>len(specs):
                raise ValueError('Bad class record')
            s=specs[sid*n:(sid+1)*n]
            if struct.unpack_from('<H',s)[0]!=oid:
                raise ValueError('Object/class ID mismatch')
            if obj['obclass']==10:
                obj['door']=dict(locked=struct.unpack_from('<H',s,6)[0],
                    other_half=struct.unpack_from('<H',s,12)[0],access=s[10])
            else:
                kind=s[6]
                obj['event']=dict(kind=TRAPS[kind] if kind<len(TRAPS) else 'unknown',
                    type=kind,comparator=struct.unpack_from('<I',s,8)[0],
                    parameters=list(struct.unpack_from('<4I',s,12)),destroy_count=s[7])
        objects.append(obj)
    # Bind to immutable baseline resources, not mutable seen/transient/save data.
    signature=digest(b''.join(struct.pack('<H',base+i)+res[base+i]
        for i in (2,4,5,7,8,19,20,22,42)))
    return dict(level_id=level,width=w,height=h,height_shift=zshift,version=version,
                signature=signature,tiles=tiles,objects=objects,texture_ids=ids)


def neighbors(i,tiles):
    x,y=tiles[i]['x'],tiles[i]['y']
    for dx,dy in DIRECTIONS:
        if 0<=x+dx<64 and 0<=y+dy<64:
            j=(y+dy)*64+x+dx
            if j in tiles:
                yield j


def door_portals(data):
    """Match the engine's one-tile door collision plane to a terrain edge.

    faceobj.c uses +/-0.5 map units along its local X axis. doorparm.h
    considers frames 0..2 closed; partial frames therefore never transmit.
    The changing bitmap silhouette is deliberately not approximated.
    """
    tiles=data['tiles'];portals=[];blocked=set();ambiguous=set()
    objects={o['id']:o for o in data['objects']}
    # A panel is two-sided: heading and heading+128 have the same plane.
    heading_edges={0:(0,2),128:(0,2),64:(1,3),192:(1,3)}
    for o in data['objects']:
        if 'door' not in o:continue
        i=o['tile'];t=tiles.get(i)
        if t is None:continue
        x,y=o['position'][:2];fx=x-math.floor(x);fy=y-math.floor(y)
        distances=(fy,1-fx,1-fy,fx)
        candidates=heading_edges.get(o.get('heading'),range(4))
        edge=min(candidates,key=lambda k:distances[k])
        dx,dy=DIRECTIONS[edge];j=(t['y']+dy)*64+t['x']+dx
        oriented=o.get('heading') in heading_edges and not o.get('pitch',0) and not o.get('bank',0)
        other=o['door'].get('other_half',0)
        partner=objects.get(other) if other else None
        partner_valid=not other or bool(partner and 'door' in partner)
        object_ids=[o['id']]+([other] if other and partner_valid else [])
        initial_frames={str(oid):objects[oid].get('frame',0) for oid in object_ids}
        common=dict(id=f'L{data["level_id"]}:P{o["id"]}',object_id=o['id'],
            object_ids=object_ids,position=o['position'],
            heading=o.get('heading'),other_half=other,initial_frames=initial_frames,
            default='open' if all(frame>=3 for frame in initial_frames.values()) else 'closed',
            runtime_gate='current_frame>=3;paired_halves_all_open',
            partial_open_policy='blocked_frames_0_1_2',panel_width=1.0,
            panel_width_source='faceobj.c fixed collision face +/-0.5 map units',
            opening_direction='bitmap_animation_unresolved')
        if not partner_valid or not oriented or distances[edge]>.01 or j not in tiles or not open_aperture(t,tiles[j],edge):
            ambiguous.add(i)
            portals.append(dict(**common,tiles=[i],edge=None,plane=None,edge_plane=None,
                panel_span=None,aperture_span=None,world_aperture_span=None,
                confidence='unresolved_interior_or_geometry'))
            continue
        pair=tuple(sorted((i,j)));blocked.add(pair)
        a=FACES[t['type']][edge];b=FACES[tiles[j]['type']][(edge+2)%4]
        lo=max(PT_OFFSETS[(a>>3)&7],1-PT_OFFSETS[b&7]);hi=min(PT_OFFSETS[a&7],1-PT_OFFSETS[(b>>3)&7])
        center=fx if edge in (0,2) else fy
        lo=max(lo,center-.5);hi=min(hi,center+.5)
        if hi-lo<.04:
            ambiguous.add(i)
            portals.append(dict(**common,tiles=[i],edge=None,plane=None,edge_plane=None,
                panel_span=None,aperture_span=None,world_aperture_span=None,
                confidence='no_panel_aperture_overlap'))
            continue
        edge_plane=t['y']+(edge==2) if edge in (0,2) else t['x']+(edge==1)
        origin=t['x'] if edge in (0,2) else t['y']
        portals.append(dict(**common,tiles=list(pair),edge=edge,
            plane=y if edge in (0,2) else x,edge_plane=edge_plane,
            panel_span=[center-.5,center+.5],aperture_span=[lo,hi],
            world_aperture_span=[origin+lo,origin+hi],
            confidence='collision_plane_and_static_aperture'))
    return portals,blocked,ambiguous


def portal_transmits(portal,frames):
    """Conservative live state: no transmission during partial opening."""
    return portal['edge'] is not None and all(frames.get(i,0)>=3 for i in portal['object_ids'])


def receiver_face_audit(profile,only_tiles=None):
    """Count fully versus partly visible flat floor/ceiling receivers.

    This is an offline risk audit, not a per-fragment proof or draw identity.
    Complex tile shapes and wall draws remain unverified.
    """
    tiles={t['id']:t for t in profile['diagnostics']['tiles']}
    member={i:t['region'] for i,t in tiles.items()}
    portals=profile['diagnostics']['door_portals']
    blocked={tuple(sorted(p['tiles'])) for p in portals if p['edge'] is not None}
    ambiguous={p['tiles'][0] for p in portals if p['edge'] is None}
    counts=Counter();examples=[];full_faces=set();partial_faces=set();any_faces=set()
    for light in profile['lights']:
        for i in light['receiver_tiles']:
            if only_tiles is not None and i not in only_tiles:continue
            t=tiles[i]
            if t['type']!=1:
                counts['complex_receiver_light_tiles']+=1
                continue
            for face in ('floor','ceiling'):
                z=t[face]+(.02 if face=='floor' else -.02)
                visible=[visible_receiver(light['position'],[t['x']+sx,t['y']+sy,z],
                    tiles,member,member[i],ambiguous,blocked)
                    for sx in (.02,.5,.98) for sy in (.02,.5,.98)]
                state='full' if all(visible) else 'partial' if any(visible) else 'none'
                counts[face+'_'+state]+=1
                if state!='none':any_faces.add((i,face))
                if state=='full':full_faces.add((i,face))
                if state=='partial':partial_faces.add((i,face))
                if state=='partial' and len(examples)<8:examples.append(dict(light=light['id'],tile=i,face=face))
    target=tiles.values() if only_tiles is None else (tiles[i] for i in only_tiles if i in tiles)
    flat_target_faces=2*sum(t['type']==1 for t in target)
    return dict(counts=dict(sorted(counts.items())),flat_target_faces=flat_target_faces,
        unique_full_faces=len(full_faces),unique_partial_faces=len(partial_faces),
        unique_any_faces=len(any_faces),partial_examples=examples,
        limitation='Nine samples are not fragment proof; wall/complex draws lack stable surface identity.')


def emissive_mask_audit(texture_bytes,rules):
    """Check reviewed source-index masks against the original owned texture file."""
    if digest(texture_bytes)!=rules['reviewed_texture_sha256']:
        raise ValueError('Reviewed emissive masks require the exact TEXTURE.RES source')
    resources=ARCHIVE(texture_bytes,1000,1363);result={}
    housing={20:{206,207,222,223},26:{54,57,58,59,60},35:{108,109},36:{108,109},52:{54,56,58,60},99:{220,221,222,223},
        82:{69,70,71,72,73},101:{213,214,215,216},
        144:{217,218,219},185:{189,190,191,223},188:{220,221,222,223},
        215:{189,190,191},216:{189,190,191},217:{217,218,219,220,221}}
    for key,indices in sorted(rules['reviewed_emissive_indices'].items(),key=lambda v:int(v[0])):
        tid=int(key);raw=resources[1000+tid];offset=int.from_bytes(raw[2:6],'little')
        frame=raw[offset:];w=int.from_bytes(frame[8:10],'little');height=int.from_bytes(frame[10:12],'little')
        pixels=frame[28:28+w*height];mask=set(indices)
        if (w,height)!=(128,128) or len(pixels)!=w*height or mask&housing[tid]:
            raise ValueError('Invalid reviewed emissive texture or housing mask')
        def share(x0,y0,x1,y1):
            return sum(pixels[y*w+x] in mask for y in range(y0,y1) for x in range(x0,x1))/((x1-x0)*(y1-y0))
        center=share(32,32,96,96);top=share(0,0,128,16)
        selected=sum(px in mask for px in pixels)
        housing_pixels=sum(px in housing[tid] for px in pixels)
        if selected<100 or housing_pixels<100:raise ValueError('Emissive/housing pixel evidence missing')
        result[key]=dict(center_selected_fraction=round(center,4),top_selected_fraction=round(top,4),
            selected_pixels=selected,housing_pixels=housing_pixels,total_pixels=len(pixels))
    return result


def segment(data):
    tiles=data['tiles'];portals,blocked,doors=door_portals(data)
    adjacency={i:[] for i in tiles}
    for i,t in tiles.items():
        for j in neighbors(i,tiles):
            u=tiles[j]
            edge=DIRECTIONS.index((u['x']-t['x'],u['y']-t['y']))
            # The source table gives both partial width and sloped edge heights.
            # Door cells remain closed in the offline default profile.
            if i not in doors and j not in doors and tuple(sorted((i,j))) not in blocked and open_aperture(t,u,edge):
                adjacency[i].append(j)
    membership={};regions=[]
    def feature(t):
        # Music/hazard/large authored-light differences are cuts; texture/fixture
        # density remain summaries to avoid turning every panel into a room.
        return (t['music'],tuple(t['hazards'].values()))
    for seed in sorted(tiles):
        if seed in membership:
            continue
        rid=f"L{data['level_id']}:R{seed:04d}";todo=[seed];members=[]
        membership[seed]=rid
        while todo:
            i=todo.pop();members.append(i)
            for j in adjacency[i]:
                u,s=tiles[j],tiles[seed]
                cur=tiles[i]
                if j not in membership and feature(u)==feature(s) and \
                        max(abs(u['darkness'][a]-cur['darkness'][a]) for a in (0,1))<=5 and \
                        abs(u['floor']-cur['floor'])<=1 and abs(u['ceiling']-cur['ceiling'])<=1.25 and \
                        abs(u['x']-s['x'])<=16 and abs(u['y']-s['y'])<=16:
                    membership[j]=rid;todo.append(j)
        members.sort();ts=[tiles[i] for i in members]
        xs=[t['x'] for t in ts];ys=[t['y'] for t in ts]
        dx=max(xs)-min(xs)+1;dy=max(ys)-min(ys)+1
        uncertain=any(not t['rectangular'] or t['safe_slab'][0]>=t['safe_slab'][1] for t in ts)
        kind=('portal' if seed in doors else 'elevator' if ts[0]['music']==7 else
              'transition_uncertain' if uncertain else 'hazard' if any(ts[0]['hazards'].values()) else
              'corridor_candidate' if max(dx,dy)>=4 and min(dx,dy)<=2 else
              'alcove_candidate' if len(ts)<=3 else 'hall_candidate' if len(ts)>=30 else 'room_candidate')
        regions.append(dict(id=rid,name=rid,origin='generated',priority=0,tiles=members,
            height_bounds=[min(t['floor'] for t in ts),max(t['ceiling'] for t in ts)],
            classification=kind,confidence=0.4 if uncertain else 0.7,
            bounds=[min(xs),min(ys),max(xs)+1,max(ys)+1],
            ambient_override=None,fog_override=None,grading_override=None))
    edges={}
    for i in tiles:
        for j in neighbors(i,tiles):
            if i>=j or membership[i]==membership[j]:
                continue
            key=(membership[i],membership[j])
            reason=('door' if i in doors or j in doors or tuple(sorted((i,j))) in blocked else
                    'blocked_geometry' if j not in adjacency[i] else 'visual_region_split')
            edges.setdefault(key,[]).append(dict(tiles=[i,j],reason=reason))
    return regions,membership,adjacency,[dict(regions=list(k),boundaries=v,
        light_transmission=any(b['reason']=='visual_region_split' for b in v)) for k,v in sorted(edges.items())]


def visible_receiver(source,target,tiles,membership,rid,doors=frozenset(),blocked=frozenset()):
    """Edge-aperture line test. Tile masks remain authoring approximations."""
    x,y=source[:2];tx,ty=target[:2];dx=tx-x;dy=ty-y
    cuts={0.,1.}
    for a,d in ((x,dx),(y,dy)):
        if d:
            for k in range(math.floor(min(a,a+d))+1,math.ceil(max(a,a+d))):
                cuts.add((k-a)/d)
    points=sorted(cuts)
    samples=[(a+b)/2 for a,b in zip(points,points[1:])]
    prev=None
    for s in samples:
        px,py=x+s*dx,y+s*dy
        cx,cy=math.floor(px),math.floor(py);i=cy*64+cx
        if not 0<=cx<64 or not 0<=cy<64 or i not in tiles or i in doors:return False
        if len(source)==3 and len(target)==3:
            z=source[2]+s*(target[2]-source[2])
            if not surface_height(tiles[i],'floor')+.01<z<surface_height(tiles[i],'ceiling')-.01:return False
        if prev is not None and prev!=i:
            if tuple(sorted((prev,i))) in blocked:return False
            a=tiles[prev];b=tiles[i]
            if abs(a['x']-b['x'])+abs(a['y']-b['y'])!=1:return False
            edge=DIRECTIONS.index((b['x']-a['x'],b['y']-a['y']))
            boundary=points[samples.index(s)]
            bx=x+boundary*dx;by=y+boundary*dy
            q=(bx-a['x']) if edge in (0,2) else (by-a['y'])
            gap=aperture(a,b,edge,q)
            if gap is None:return False
            if len(source)==3 and len(target)==3 and not gap[0]+.01<source[2]+boundary*(target[2]-source[2])<gap[1]-.01:return False
        prev=i
    return True


def apply_overlay(profile,overlay):
    """Exact-ID replacement/deletion only; never modifies the generated layer."""
    if overlay.get('schema_version')!=VERSION or overlay.get('baseline_signature')!=profile['baseline_signature']:
        raise ValueError('Overlay version/signature mismatch; explicit rebase required')
    result=copy.deepcopy(profile)
    if set(overlay)-{'schema_version','baseline_signature','patches'}:
        raise ValueError('Unknown overlay key')
    for patch in overlay.get('patches',[]):
        if set(patch)!={'collection','id','replacement'} or patch['collection'] not in ('regions','lights','material_rules','dynamic_overrides'):
            raise ValueError('Invalid patch')
        values=result[patch['collection']];matches=[i for i,v in enumerate(values) if v['id']==patch['id']]
        if len(matches)!=1:
            raise ValueError('Stale or ambiguous override target')
        if patch['replacement'] is None:
            del values[matches[0]]
        else:
            replacement=copy.deepcopy(patch['replacement'])
            if replacement.get('id')!=patch['id']:
                raise ValueError('Override cannot change stable ID')
            replacement['origin']='manual';values[matches[0]]=replacement
    validate(result)
    return result


def generate(data,props,rules,objprops=None):
    tiles=data['tiles'];level=data['level_id'];regions,member,adjacency,edges=segment(data)
    portals,blocked,door_tiles=door_portals(data)
    for portal in portals:
        portal['connected_regions']=sorted({member[i] for i in portal['tiles']})
    classes={tid:kind for kind,ids in rules['classes'].items() for tid in ids}
    for tid in rules.get('deck_luminous_fixture',{}).get(str(level),[]):
        classes[tid]='luminous_fixture'
    archetype=copy.deepcopy(rules['archetypes'][str(level)])
    candidates=[];lights=[];warnings=[]
    for i,t in tiles.items():
        t['region']=member[i];t['material_classes']={f:classes.get(tid,'unknown') for f,tid in t['textures'].items()}
        for face,tid in t['textures'].items():
            if classes.get(tid) not in ('luminous_fixture','monitor_display'):
                continue
            fixture=classes[tid]=='luminous_fixture' and not props[tid]['force_dir']
            emission=emission_policy(rules,classes[tid],tid,face)
            confident=fixture and emission=='emits_normal' and t['type'] in (1,*range(6,18))
            floor_low=fixture and emission=='emits_low' and face=='floor' and t['type'] in (1,*range(6,18))
            # A wall assignment is only a visible face candidate if an edge is
            # actually closed; the tile's generic wall ID is not every side.
            wall_edges=[e for e,(dx,dy) in enumerate(DIRECTIONS) if
                (t['y']+dy)*64+t['x']+dx not in tiles or
                not open_aperture(t,tiles[(t['y']+dy)*64+t['x']+dx],e)] if face=='wall' else []
            wall_low=fixture and bool(wall_edges) and emission=='emits_low'
            position=[t['x']+.5,t['y']+.5,
                surface_height(t,'ceiling')-.08 if face=='ceiling' else
                surface_height(t,'floor')+.08 if face=='floor' else
                (surface_height(t,'floor')+surface_height(t,'ceiling'))/2]
            if wall_low:
                e=wall_edges[0];position[0]+=(0,.42,0,-.42)[e];position[1]+=(-.42,0,.42,0)[e]
            candidates.append(dict(id=f'L{level}:T{i}:{face}:{tid}',tile=i,source_resource_id=tid,
                source_object_id=None,face=face,position=position,
                confidence=.9 if confident else .68 if floor_low or wall_low else .55,classification=classes[tid],
                policy='local_light' if confident or floor_low or wall_low else 'emissive_review',
                rgb=rules['fixture_rgb'].get(str(tid),[.7,.85,1.]),
                reason='Reviewed luminous fixture on exposed surface' if confident or floor_low or wall_low else
                'Visible face, pixel mask or orientation requires review'))
    for o in data['objects']:
        prop=(objprops or {}).get(o['triple'])
        if prop:
            o['render_properties']=prop
            light_type=(prop['flags']>>6)&3
            o['unlit']=light_type==2 or (light_type==3 and bool(o['flags']&4))
        text_bitmap=bool(prop and prop['render_type']==8 and o['triple']==0x70203)
        display=o['triple'] in DISPLAY_TRIPLES or (prop and prop['render_type']==1) or text_bitmap
        kind='luminous_fixture' if o['obclass']==7 and o['subclass']==3 else 'monitor_display' if display else None
        if kind is None:
            continue
        t=tiles.get(o['tile']);confident=kind=='luminous_fixture' and t and t['type']==1 and t['floor']<=o['position'][2]<=t['ceiling']+.02
        object_position=list(o['position'])
        if confident:object_position[2]=min(object_position[2],surface_height(t,'ceiling')-.12)
        candidates.append(dict(id=f'L{level}:O{o["id"]}',tile=o['tile'],source_resource_id=None,
            source_object_id=o['id'],face='object',position=object_position,confidence=.85 if confident else .55,
            classification='text_bitmap_display' if text_bitmap else kind,
            policy='local_light' if confident else 'emissive_review',
            rgb=[1.,.85,.65],reason='Named lamp; bulb offset/color are assumptions' if kind=='luminous_fixture' else
            'Text bitmap is a display candidate, not a physical lamp' if text_bitmap else
            'Display render property or named screen; visible brightness does not imply local-light emission'))
    budget=Counter();g=rules['generation']
    energy=Counter()
    for c in sorted(candidates,key=lambda c:(-c['confidence'],c['id'])):
        if c['policy']!='local_light' or c['tile'] not in tiles:
            continue
        t=tiles[c['tile']];rid=member[c['tile']]
        if c['tile'] in door_tiles:
            c['policy']='emissive_review';continue
        radius=g['object_radius' if c['face']=='object' else 'ceiling_radius']
        if any(math.dist(light['position'],c['position'])<1.4 and
               light['source_resource_id']==c['source_resource_id'] for light in lights):
            c['policy']='suppressed_fixture_cluster';continue
        rec=[]
        for i,u in tiles.items():
            if i in door_tiles:continue
            if math.dist(c['position'][:2],[u['x']+.5,u['y']+.5])<radius and any(
                    visible_receiver(c['position'],[u['x']+sx,u['y']+sy,
                        (surface_height(u,'floor')+surface_height(u,'ceiling'))/2],
                        tiles,member,rid,door_tiles,blocked) for sx,sy in ((.5,.5),(.25,.5),(.75,.5),(.5,.25),(.5,.75))):
                rec.append(i)
        if not rec:
            c['policy']='suppressed_budget_or_aperture';continue
        base=g['object_intensity' if c['face']=='object' else 'ceiling_intensity']
        if c['face'] in ('floor','wall'):base*=.28
        base*=.35+.65*(1-sum(t['darkness'])/32)
        # Dominant nearby fixture survives; later repeats spend the remaining
        # per-receiver energy rather than stacking four full-strength panels.
        eligible=[i for i in rec if budget[i]<g['max_lights_per_receiver'] and
                  energy[i]<g.get('max_receiver_energy',1.8)]
        if not eligible:
            c['policy']='suppressed_budget_or_aperture';continue
        cap=min(base,min(g.get('max_receiver_energy',1.8)-energy[i] for i in eligible))
        if cap<.08:c['policy']='suppressed_energy';continue
        rec=eligible;budget.update(rec);energy.update({i:cap for i in rec})
        lights.append(dict(id=c['id'],origin='generated',position=c['position'],rgb=c['rgb'],
            intensity=round(cap,4),radius=radius,
            falloff='squared_linear',receiver_regions=sorted({member[i] for i in rec}),receiver_tiles=sorted(rec),
            state_condition={'kind':'object_active','object_id':c['source_object_id']} if c['source_object_id'] else None,
            source_resource_id=c['source_resource_id'],source_object_id=c['source_object_id'],
            confidence=c['confidence'],parameter_confidence=.45))
    for r in regions:
        ts=[tiles[i] for i in r['tiles']]
        r['statistics']=dict(mean_darkness=[round(sum(t['darkness'][a] for t in ts)/len(ts),3) for a in (0,1)],
            material_counts=dict(sorted(Counter(c for t in ts for c in t['material_classes'].values()).items())),
            fixture_candidates=sum(c['tile'] in r['tiles'] for c in candidates))
        # Hazard alone is not evidence of airborne particles. Keep suggestions off.
        if r['classification'] in ('hazard','hall_candidate','corridor_candidate'):
            r['fog_override']=dict(enabled=False,rgb=archetype['fog_rgb'],density=0.,model='exp',
                start=4.,end=16.,height_influence=None,state_channel=None,
                suggestion='Review sparse distance haze; geometry/hazard does not prove mist',confidence=.3)
    events=[dict(object_id=o['id'],tile=o['tile'],**o['event']) for o in data['objects'] if 'event' in o and o['event']['type']]
    materials=[dict(id=f'texture:{tid}',origin='generated',resource_id=tid,family_id=props[tid]['family_texture'],
        animation_group=props[tid]['anim_group'],animation_position=props[tid]['group_pos'],
        classification=classes.get(tid,'unknown'),emissive=False,
        emissive_indices=rules.get('reviewed_emissive_indices',{}).get(str(tid),[]),
        emissive_gain=1.,grading_policy='preserve_original_art',
        local_light_policy='surface_aware' if classes.get(tid)=='luminous_fixture' else 'none',
        surface_emission={face:emission_policy(rules,classes.get(tid,'unknown'),tid,face)
            for face in ('ceiling','floor','wall')},
        special_handling='sky_excluded' if props[tid]['force_dir'] else
            'reviewed_source_index_mask' if str(tid) in rules.get('reviewed_emissive_indices',{}) else
            'pixel_mask_review') for tid in sorted(set(data['texture_ids']))]
    warnings.extend(['DIAGNOSTIC ONLY: no runtime loader or visual acceptance.',
        'Face-table apertures and sloped edge heights model static optical connections; internal complex walls remain unresolved.',
        'Receiver tiles use five samples; fragment-safe masks and dynamic invalidation required before runtime.',
        'All door portals block generated light; open-door spill is not implemented.',
        'Only reviewed fixture textures carry source-index emissive masks; other mixed art remains non-emissive.',
        'No automatic fog enabled; no generic power/alarm/SHODAN color state invented.',
        'Fullbright render flags and UNLIT instances are inventoried, not universal light emitters.'])
    if objprops is None:warnings.append('OBJPROP omitted: generic TEXTPOLY display and unlit detection unavailable.')
    p=dict(schema_version=VERSION,generator_version='horror-author1c.1',origin='generated',
        level_id=level,baseline_signature=data['signature'],rules_signature=digest(encode(rules)),
        texture_properties_signature=digest(encode(props)),
        object_properties_signature=digest(encode(objprops)) if objprops is not None else None,
        units={'position':'map tile X/Y and positive map-height Z, 1 tile unit; not meters',
               'rgb':'linear illumination RGB, 0..1','intensity':'relative multiplier','density':'inverse tile'},
        defaults=dict(archetype=archetype['name'],ambient_rgb=archetype['ambient_rgb'],ambient_intensity=archetype['ambient_intensity'],
            darkness_response={'input':'live_original_scalar_darkness','normalized_white':0,'normalized_black':16,'exponent':2.,'floor':0.},
            fog=dict(enabled=False,rgb=archetype['fog_rgb'],density=0.,model='exp',start=4.,end=16.,height_influence=None,state_channel=None),
            grading={'saturation':1.,'exposure':0.}),
        regions=regions,lights=lights,material_rules=materials,dynamic_overrides=[],
        diagnostics=dict(tiles=list(tiles.values()),objects=data['objects'],candidates=candidates,region_graph=edges,
            door_portals=portals,
            events=events,warnings=warnings,
            summary=dict(non_solid_tiles=len(tiles),flat_tiles=sum(t['type']==1 for t in tiles.values()),
                regions=len(regions),singleton_regions=sum(len(r['tiles'])==1 for r in regions),
                door_objects=sum('door' in o for o in data['objects']),candidates=len(candidates),lights=len(lights),
                receiver_tiles=len(budget),max_lights_per_receiver=max(budget.values(),default=0),
                fog_enabled=0,lighting_events=sum(e['kind']=='lighting' for e in events),
                material_classes=dict(Counter(m['classification'] for m in materials)))))
    validate(p)
    return p


def validate(p):
    validate_contract(p,SCHEMA)
    if p.get('schema_version')!=VERSION or p.get('level_id') not in (0,1,2,3,4,5,6,7,9):
        raise ValueError('Unsupported profile version/level')
    encode(p)  # Reject NaN/infinity.
    for field in ('baseline_signature','rules_signature','texture_properties_signature'):
        if len(p[field])!=64 or any(c not in '0123456789abcdef' for c in p[field]):
            raise ValueError('Invalid signature')
    regions={r['id']:r for r in p['regions']};seen=set()
    for collection in ('regions','lights','material_rules','dynamic_overrides'):
        ids=[v['id'] for v in p[collection]]
        if len(ids)!=len(set(ids)):raise ValueError('Duplicate stable ID')
    for r in regions.values():
        if not r['tiles'] or seen.intersection(r['tiles']) or any(type(i)!=int or not 0<=i<4096 for i in r['tiles']):
            raise ValueError('Invalid/overlapping region mask')
        seen.update(r['tiles'])
    if seen!={t['id'] for t in p['diagnostics']['tiles']}:
        raise ValueError('Incomplete region coverage')
    budget=Counter()
    for light in p['lights']:
        if len(light['rgb'])!=3 or any(not 0<=c<=1 for c in light['rgb']) or not 0<light['radius']<=16 or not 0<=light['intensity']<=8:
            raise ValueError('Unsafe light range')
        if len(light['position'])!=3 or not all(math.isfinite(x) for x in light['position']):
            raise ValueError('Invalid light position')
        if not light['receiver_regions'] or any(r not in regions for r in light['receiver_regions']):
            raise ValueError('Unknown receiver region')
        allowed={i for r in light['receiver_regions'] for i in regions[r]['tiles']}
        if not light['receiver_tiles'] or not set(light['receiver_tiles'])<=allowed:
            raise ValueError('Receiver outside region')
        budget.update(set(light['receiver_tiles']))
    if max(budget.values(),default=0)>4:raise ValueError('Receiver light budget exceeded')
    for material in p['material_rules']:
        mask=material['emissive_indices']
        if (material['emissive'] and mask or mask and material['special_handling']!='reviewed_source_index_mask' or
                len(mask)!=len(set(mask)) or any(type(i)!=int or not 0<=i<=255 for i in mask)):
            raise ValueError('Invalid emissive source-index mask')
    targets=set(regions)|{x['id'] for x in p['lights']}|{x['id'] for x in p['material_rules']}
    for override in p['dynamic_overrides']:
        if any(target not in targets for target in override['targets']):raise ValueError('Unknown dynamic target')
    for fog in [p['defaults']['fog']]+[r['fog_override'] for r in p['regions'] if r['fog_override']]:
        if fog['end']<=fog['start']:raise ValueError('Invalid fog interval')


def validate_contract(value,schema,path='$'):
    """Dependency-free checker for keywords used by the shipped v1 contract.

    Not a general JSON Schema library; schemas are trusted repository files.
    It rejects unknown validation keywords so extending the contract cannot
    silently bypass checking. The public contract is also standard JSON Schema.
    """
    supported={'$schema','$id','title','description','$defs','$ref','type','const','enum','anyOf','oneOf',
               'required','properties','additionalProperties','items','minItems','maxItems','uniqueItems',
               'minimum','maximum','exclusiveMinimum','pattern'}
    if set(schema)-supported:raise ValueError('Unsupported schema keyword')
    if '$ref' in schema:
        ref=schema['$ref']
        if not ref.startswith('#/$defs/'):raise ValueError('External schema references not allowed')
        return validate_contract(value,SCHEMA['$defs'][ref.split('/')[-1]],path)
    for alternative in ('anyOf','oneOf'):
        if alternative in schema:
            matches=0
            for child in schema[alternative]:
                try:validate_contract(value,child,path);matches+=1
                except ValueError:pass
            if matches<1 or (alternative=='oneOf' and matches!=1):raise ValueError(f'{path}: no unique allowed shape')
    if 'type' in schema:
        types=schema['type'] if isinstance(schema['type'],list) else [schema['type']]
        actual=('null' if value is None else 'boolean' if type(value)==bool else 'integer' if type(value)==int else
                'number' if type(value)==float else 'string' if isinstance(value,str) else
                'array' if isinstance(value,list) else 'object' if isinstance(value,dict) else 'unknown')
        if actual not in types and not (actual=='integer' and 'number' in types):raise ValueError(f'{path}: wrong type')
    def same(a,b):return a==b and isinstance(a,bool)==isinstance(b,bool)
    if 'const' in schema and not same(value,schema['const']):raise ValueError(f'{path}: wrong constant')
    if 'enum' in schema and not any(same(value,v) for v in schema['enum']):raise ValueError(f'{path}: unknown enum')
    if isinstance(value,dict):
        if set(schema.get('required',[]))-set(value):raise ValueError(f'{path}: missing field')
        properties=schema.get('properties',{});additional=schema.get('additionalProperties',True)
        for key,item in value.items():
            child=properties.get(key,additional)
            if child is False:raise ValueError(f'{path}.{key}: unknown field')
            if isinstance(child,dict):validate_contract(item,child,f'{path}.{key}')
    if isinstance(value,list):
        if len(value)<schema.get('minItems',0) or len(value)>schema.get('maxItems',math.inf):raise ValueError(f'{path}: array size')
        if schema.get('uniqueItems') and len({json.dumps(v,sort_keys=True) for v in value})!=len(value):raise ValueError(f'{path}: duplicate array value')
        for i,item in enumerate(value):
            if 'items' in schema:validate_contract(item,schema['items'],f'{path}[{i}]')
    if type(value) in (int,float):
        if not math.isfinite(value) or value<schema.get('minimum',-math.inf) or value>schema.get('maximum',math.inf) or value<=schema.get('exclusiveMinimum',-math.inf):raise ValueError(f'{path}: out of range')
    if isinstance(value,str) and 'pattern' in schema and not re.search(schema['pattern'],value):raise ValueError(f'{path}: invalid string')


def svg_map(p,mode):
    tiles=p['diagnostics']['tiles'];regions={r['id']:r for r in p['regions']}
    candidates=Counter(c['tile'] for c in p['diagnostics']['candidates'])
    receivers=Counter(i for light in p['lights'] for i in light['receiver_tiles'])
    suppressed=Counter(c['tile'] for c in p['diagnostics']['candidates'] if c['policy'].startswith('suppressed'))
    clusters=Counter(c['tile'] for c in p['diagnostics']['candidates'] if c['policy']=='suppressed_fixture_cluster')
    colors={'luminous_fixture':'#a6fbff','monitor_display':'#46bad0','clinical_panel':'#4876ae',
        'industrial_wall':'#b28a47','decorative':'#976675','non_emissive_bright':'#5a6070','damaged_surface':'#b73536','organic_grove':'#389957','machinery':'#8b7eb9','unknown':'#494952'}
    result=['<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1120" viewBox="0 0 1024 1120">',
        '<rect width="1024" height="1120" fill="#11151d"/>',
        f'<text x="20" y="26" fill="white" font-size="20">HORROR-AUTHOR1 | Level {p["level_id"]} | {mode} | DIAGNOSTIC</text>']
    for t in tiles:
        r=regions[t['region']]
        if mode=='regions':
            color='#'+digest(r['id'].encode())[:6]
        elif mode in ('floor-darkness','ceiling-darkness'):
            v=255-round(t['darkness'][mode=='ceiling-darkness']/15*240);color=f'#{v:02x}{v:02x}{v:02x}'
        elif mode=='materials':color=colors[t['material_classes']['wall']]
        elif mode=='receivers':color=('#%02x%02x%02x'%(30+min(4,receivers[t['id']])*40,70,95)) if receivers[t['id']] else '#27313a'
        elif mode=='portals':color='#405a65' if t['rectangular'] else '#735634'
        elif mode=='suppressed':color='#f0805a' if suppressed[t['id']] else '#35444f'
        elif mode=='clusters':color='#dfbb61' if clusters[t['id']] else '#35444f'
        elif mode=='emissive':color='#a67a4f' if any(m['resource_id'] in t['textures'].values() and m['special_handling']=='pixel_mask_review' and m['classification']=='luminous_fixture' for m in p['material_rules']) else '#35444f'
        elif mode=='hazards-state':color='#c449b6' if t['hazards']['bio'] else '#cbd24d' if t['hazards']['radiation'] else '#be563e' if t['hazards']['peril'] else '#3986af' if t['music']==7 else '#38414a'
        else:color='#9ae7ff' if candidates[t['id']] else '#38414a'
        title=html.escape(json.dumps(dict(tile=[t['x'],t['y']],region=r['id'],kind=r['classification'],darkness=t['darkness'],materials=t['material_classes'])))
        x=t['x']*15+32;y=t['y']*15+48
        result.append(f'<rect x="{x}" y="{y}" width="14" height="14" fill="{color}"><title>{title}</title></rect>')
        if mode=='regions' and t['id']==min(r['tiles']):
            result.append(f'<text x="{x}" y="{y+10}" fill="white" font-size="7">{t["id"]}</text>')
        if not t['rectangular'] or t['safe_slab'][0]>=t['safe_slab'][1]:
            result.append(f'<path d="M{x},{y}l14,14" stroke="#ffb871" stroke-width="1"/>')
    for o in p['diagnostics']['objects']:
        if 'door' in o or (mode=='hazards-state' and o.get('event',{}).get('type',0)):
            x=o['position'][0]*15+32;y=o['position'][1]*15+48
            color='#ff4747' if 'door' in o else '#ffffff'
            result.append(f'<rect x="{x-3}" y="{y-3}" width="6" height="6" fill="{color}"><title>Object {o["id"]}</title></rect>')
    if mode in ('fixtures','receivers'):
        for light in p['lights']:
            x,y=light['position'][:2];radius=light['radius']*15
            result.append(f'<circle cx="{x*15+32}" cy="{y*15+48}" r="{radius}" fill="none" stroke="#ffe58c" stroke-opacity=".45"><title>{html.escape(light["id"])}</title></circle>')
    if mode=='portals':
        for portal in p['diagnostics']['door_portals']:
            x,y=portal['position'][:2]
            result.append(f'<circle cx="{x*15+32}" cy="{y*15+48}" r="5" fill="#ff6565"><title>Closed default portal {portal["object_id"]}: {portal["confidence"]}</title></circle>')
    legends={'regions':'Colors = inferred regions; numbers = seed tile IDs; red = doors.',
        'floor-darkness':'White = original darkness 0; dark = 15. No generated illumination shown.',
        'ceiling-darkness':'White = original darkness 0; dark = 15. Independent ceiling channel.',
        'fixtures':'Cyan = candidate tile; gold circle = generated radius, NOT receiving area.',
        'materials':'Wall assignment: cyan fixture/display, blue clinical, ochre industrial, red damaged.',
        'hazards-state':'Magenta bio; yellow radiation; rust peril; blue elevator zone; white event object.',
        'receivers':'Brighter tile = more generated receiver masks; gold rings = source radii.',
        'portals':'Red dots = default-closed door portals; orange = unresolved shape.',
        'suppressed':'Orange = suppressed fixture candidate. Hover source JSON for reason.',
        'clusters':'Gold = repeated fixture candidates suppressed into a cluster.',
        'emissive':'Brown = luminous mixed art requiring a reviewed pixel mask; no whole-texture glow.'}
    result.append(f'<text x="32" y="1040" fill="white" font-size="15">{legends[mode]}</text>')
    result.append('<text x="32" y="1067" fill="#ffb871" font-size="15">Orange slash = diagonal or closed conservative slab. Hover tiles for IDs and source data.</text></svg>')
    return '\n'.join(result)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive',type=Path,required=True)
    parser.add_argument('--textprop',type=Path,required=True)
    parser.add_argument('--objprop',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--levels',type=int,nargs='+',default=[1,6],choices=[0,1,2,3,4,5,6,7,9])
    parser.add_argument('--rules',type=Path,default=RULES)
    parser.add_argument('--overlay',type=Path)
    args=parser.parse_args()
    if args.overlay and len(args.levels)!=1:parser.error('Overlay requires exactly one level')
    raw=args.archive.read_bytes();res=ARCHIVE(raw,4000,5400);props=PROPS(args.textprop)
    rules=json.loads(args.rules.read_text())
    objprops=object_properties(args.objprop) if args.objprop else None
    inputs={p.resolve() for p in (args.archive,args.textprop,args.objprop,args.rules,args.overlay) if p}
    modes=('regions','floor-darkness','ceiling-darkness','fixtures','materials','hazards-state',
           'receivers','portals','suppressed','clusters','emissive')
    outputs=[args.output/f'level-{l}.{suffix}' for l in args.levels
             for suffix in ('generated.json','merged.json',*(m+'.svg' for m in modes))]
    if any(p.resolve() in inputs for p in outputs):raise ValueError('Output would overwrite an input/overlay')
    args.output.mkdir(parents=True,exist_ok=True)
    for level in args.levels:
        p=generate(load_level(res,level,props),props,rules,objprops)
        p['source_archive_sha256']=digest(raw)
        target=args.output/f'level-{level}.generated.json'
        merged=None
        if args.overlay:
            merged=apply_overlay(p,json.loads(args.overlay.read_text()))
        target.write_bytes(encode(p))
        if merged is not None:
            (args.output/f'level-{level}.merged.json').write_bytes(encode(merged))
        for mode in modes:
            (args.output/f'level-{level}.{mode}.svg').write_text(svg_map(p,mode),encoding='utf-8')
        print(json.dumps({'level':level,**p['diagnostics']['summary']},sort_keys=True))


if __name__=='__main__':
    main()
