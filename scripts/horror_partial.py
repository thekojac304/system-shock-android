"""AUTHOR1E offline convex-cell beams. No inference or runtime map traversal.

Only renderer types 1..9 are admitted. A beam is a chain of convex cells and
their shared apertures: every admitted segment has endpoints in each convex
cell. Unsupported cells and all unvalidated door cells are opaque. Sampling
is diagnostic only; it never grants visibility.
"""
import argparse
from collections import Counter, deque
import json
import math
from pathlib import Path
import horror_author as h
import horror_surface as s

EPS = 1e-8
GUARD = 2e-5
MAX_POLYGONS = 4
MAX_VERTICES = 8
MAX_STATES = 512
MAX_DEPTH = 12
MAX_LIGHTS = 4


def dot(a,b): return sum(x*y for x,y in zip(a,b))
def sub(a,b): return [x-y for x,y in zip(a,b)]
def cross(a,b): return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def mean(points): return [sum(p[k] for p in points)/len(points) for k in range(len(points[0]))]


def clip(poly, plane):
    """Keep dot(n,p)+d >= 0; no epsilon expansion of the lit region."""
    out=[]
    if not poly:return out
    for a,b in zip(poly,poly[1:]+poly[:1]):
        da=dot(plane[:-1],a)+plane[-1];db=dot(plane[:-1],b)+plane[-1]
        if da>=0:out.append(a)
        if (da<0)!=(db<0):out.append([x+(y-x)*da/(da-db) for x,y in zip(a,b)])
    clean=[]
    for p in out:
        if not clean or sum((x-y)**2 for x,y in zip(p,clean[-1]))>EPS**2:clean.append(p)
    if len(clean)>1 and sum((x-y)**2 for x,y in zip(clean[0],clean[-1]))<EPS**2:clean.pop()
    return clean if len(clean)>=3 else []


def clipped(poly,planes):
    for plane in planes:
        poly=clip(poly,plane)
        if not poly:break
    return poly


def area(poly):
    return abs(sum(a[0]*b[1]-a[1]*b[0] for a,b in zip(poly,poly[1:]+poly[:1])))/2 if poly else 0


def hull(points):
    pts=sorted(set(tuple(p) for p in points))
    def turn(a,b,c):return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
    lower=[];upper=[]
    for seq,out in ((pts,lower),(pts[::-1],upper)):
        for p in seq:
            # A positive turn, however small, can be an extreme corner when X
            # coordinates nearly tie. Epsilon here can SHRINK the hull, making
            # the union-area test accept a concave (unsafe) merge.
            while len(out)>1 and turn(out[-2],out[-1],p)<=0:out.pop()
            out.append(p)
    return [list(p) for p in lower[:-1]+upper[:-1]]


def polygon_planes(poly,guard=0):
    poly=hull(poly);planes=[]
    for a,b in zip(poly,poly[1:]+poly[:1]):
        dx=b[0]-a[0];dy=b[1]-a[1];n=math.hypot(dx,dy)
        planes.append([-dy/n,dx/n,(dy*a[0]-dx*a[1])/n-guard])
    return planes


def merge_polygons(polys):
    # Merge only when the convex hull adds no area to the exact union.
    polys=[hull(p) for p in polys if area(p)>EPS]
    changed=True
    while changed:
        changed=False
        for i in range(len(polys)):
            for j in range(i+1,len(polys)):
                combined=hull(polys[i]+polys[j])
                overlap=clipped(polys[i],polygon_planes(polys[j]))
                union=area(polys[i])+area(polys[j])-area(overlap)
                if area(combined)>=max(area(polys[i]),area(polys[j]))-1e-12 and abs(area(combined)-union)<1e-12:
                    polys[i]=combined;polys.pop(j);changed=True;break
            if changed:break
    return sorted(polys,key=lambda p:tuple(tuple(v) for v in p))


def footprint(t):
    x=t['x'];y=t['y']
    # frtables.c point codes 4,8,C,0 -> NW,NE,SE,SW.
    corners=[[x,y],[x+1,y],[x+1,y+1],[x,y+1]]
    picks={1:(0,1,2,3),2:(1,2,3),3:(2,3,0),4:(0,1,3),5:(0,1,2)}
    return [corners[i] for i in picks.get(t['type'],(0,1,2,3))]


def height(t,face,x,y):
    z=t[face];kind=t['type']
    if kind<6:return z
    if face=='floor' and t['mirror']==3 or face=='ceiling' and t['mirror']==2:return z
    q={6:1-y,7:x,8:y,9:1-x}[kind]
    if face=='ceiling' and t['mirror'] in (0,3):q=1-q
    return z+t['param']/2**t['height_shift']*q*(1 if face=='floor' else -1)


def volume(t):
    if not 1<=t['type']<=9:return None
    if t['type']<=5 and t['param']!=0:return None
    planes=[[a,b,0,c] for a,b,c in polygon_planes(footprint(t))]
    for face,sign in (('floor',1),('ceiling',-1)):
        z=height(t,face,0,0);dx=height(t,face,1,0)-z;dy=height(t,face,0,1)-z
        planes.append([-sign*dx,-sign*dy,sign,sign*(dx*t['x']+dy*t['y']-z)])
    if any(height(t,'ceiling',x-t['x'],y-t['y'])<=height(t,'floor',x-t['x'],y-t['y']) for x,y in footprint(t)):return None
    return planes


def edge_polygon(t,edge):
    x=t['x'];y=t['y']
    a,b=([[x,y],[x+1,y]],[[x+1,y],[x+1,y+1]],[[x+1,y+1],[x,y+1]],[[x,y+1],[x,y]])[edge]
    return [[*a,-8],[*b,-8],[*b,8],[*a,8]]


def cone(source,poly):
    center=mean(poly);planes=[]
    for a,b in zip(poly,poly[1:]+poly[:1]):
        n=cross(sub(a,source),sub(b,source));length=math.sqrt(dot(n,n))
        if length<EPS:continue
        n=[v/length for v in n]
        if dot(n,sub(center,source))<0:n=[-v for v in n]
        planes.append(n+[-dot(n,source)])
    return planes


def beams(source,tiles,door_tiles,targets):
    start=math.floor(source[1])*64+math.floor(source[0]);volumes={i:volume(t) for i,t in tiles.items()}
    if start not in volumes or volumes[start] is None or start in door_tiles:return {},'unsupported_source'
    if any(dot(p[:3],source)+p[3]<=EPS for p in volumes[start]):return {},'source_outside_volume'
    xs=[tiles[i]['x'] for i in targets]+[source[0]];ys=[tiles[i]['y'] for i in targets]+[source[1]]
    bounds=(math.floor(min(xs)),math.floor(max(xs)),math.floor(min(ys)),math.floor(max(ys)))
    queue=deque([(start,[],[])]);result={};visited=0
    while queue:
        index,planes,path=queue.popleft();visited+=1
        if visited>MAX_STATES:return {},'beam_complexity'
        if index in targets:result.setdefault(index,[]).append((planes,path))
        if len(path)>=MAX_DEPTH:continue # omission is safe
        t=tiles[index]
        for edge,(dx,dy) in enumerate(h.DIRECTIONS):
            xx=t['x']+dx;yy=t['y']+dy;j=yy*64+xx
            if not bounds[0]<=xx<=bounds[1] or not bounds[2]<=yy<=bounds[3]:continue
            if j not in volumes or volumes[j] is None or j in door_tiles:continue
            # Straight segments are monotone in X/Y. Crossing away from source
            # also excludes cycles and coplanar ambiguous source boundaries.
            axis=0 if dx else 1;coordinate=(t['x']+(dx>0)) if dx else (t['y']+(dy>0))
            sign=dx or dy
            if sign*(coordinate-source[axis])<=EPS:continue
            ap=clipped(edge_polygon(t,edge),volumes[index]+volumes[j]+planes)
            if not ap:continue
            n=[0,0,0];n[axis]=sign
            onward=n+[-sign*coordinate]
            queue.append((j,planes+cone(source,ap)+[onward],path+[[index,j,edge]]))
    return result,None


def faces(level,t,tiles,solid_keys):
    if volume(t) is None:return []
    result=[]
    for face in ('floor','ceiling'):
        poly=[[x,y,height(t,face,x-t['x'],y-t['y'])] for x,y in footprint(t)]
        result.append((face,s.surface_id(level,t,face),poly,(0,1)))
    if t['type'] in (2,3,4,5):
        # wall_pts normal diagonal endpoints, same IDs as tile_walls.
        wall={2:(1,[(0,1),(1,0)]),3:(3,[(0,0),(1,1)]),4:(2,[(1,0),(0,1)]),5:(0,[(1,1),(0,0)])}[t['type']]
        a,b=[[t['x']+x,t['y']+y] for x,y in wall[1]]
        poly=[[*a,t['floor']],[*b,t['floor']],[*b,t['ceiling']],[*a,t['ceiling']]]
        sid=[level,t['x'],t['y'],4,wall[0],0,t['type'],s.geometry_key(t),0]
        result.append(('diagonal_wall',sid,poly,(0,2)))
    for edge,(dx,dy) in enumerate(h.DIRECTIONS):
        j=(t['y']+dy)*64+t['x']+dx;other=tiles.get(j)
        if other is not None and h.FACES[other['type']][(edge+2)%4]!=255:continue
        if other is None and j not in solid_keys:continue
        poly=clipped(edge_polygon(t,edge),volume(t))
        if not poly:continue
        sid=[level,t['x'],t['y'],3,edge,0,t['type'],s.geometry_key(t),s.geometry_key(other) if other else solid_keys[j]]
        result.append(('wall',sid,poly,(0,2) if edge in (0,2) else (1,2)))
    return result


def receiver(poly,axes,beam_list):
    origin=poly[0];local=lambda p:[p[k]-origin[k] for k in axes]
    base=hull([local(p) for p in poly]);pieces=[];paths=[]
    for planes,path in beam_list:
        clipped_face=clipped(poly,planes)
        if not clipped_face:continue
        p=hull([local(v) for v in clipped_face])
        if area(p)>EPS:pieces.append(p);paths.append(path)
    pieces=merge_polygons(pieces)
    if len(pieces)>MAX_POLYGONS or any(len(p)>MAX_VERTICES for p in pieces):return 'unsupported',[],paths,'polygon_complexity'
    if len(pieces)==1 and all(dot(plane[:2],vertex)+plane[2]>=0
                             for plane in polygon_planes(pieces[0]) for vertex in base):
        return 'full',[],paths,None
    # Contract inward in face-local coordinates after all unions. A shader must
    # use highp and the same axes/origin; quantization needs a separate test.
    pieces=[clipped(p,polygon_planes(p,GUARD)) for p in pieces]
    pieces=[p for p in pieces if area(p)>EPS]
    return ('partial' if pieces else 'blocked'),pieces,paths,None


def distance_to_face(source,poly):
    """Exact convex planar polygon distance, for effective falloff coverage."""
    a=poly[0];normal=cross(sub(poly[1],a),sub(poly[2],a));n2=dot(normal,normal)
    if n2<EPS**2:return math.inf
    offset=dot(normal,sub(source,a))/n2
    projected=[source[k]-normal[k]*offset for k in range(3)]
    center=mean(poly)
    inside=True;best=math.inf
    for u,v in zip(poly,poly[1:]+poly[:1]):
        edge=sub(v,u);plane=cross(normal,edge)
        if dot(plane,sub(center,u))*dot(plane,sub(projected,u))<0:inside=False
        e2=dot(edge,edge)
        if e2>0:
            t=max(0,min(1,dot(sub(source,u),edge)/e2))
            point=[u[k]+edge[k]*t for k in range(3)]
            best=min(best,math.sqrt(sum((source[k]-point[k])**2 for k in range(3))))
    return abs(offset)*math.sqrt(n2) if inside else best


def segment_witness(source,target,tiles,door_tiles):
    """Independent grid-interval oracle. It uses face-table edge heights, not
    beam planes. Each cell interval is checked at both ends against its linear
    floor/ceiling and diagonal halfspace. Returns first blocking cell/boundary.
    Used for diagnostics/tests ONLY, never to authorize a receiver.
    """
    delta=sub(target,source);cuts={0.,1.}
    for axis in (0,1):
        if abs(delta[axis])<EPS:continue
        for coordinate in range(math.floor(min(source[axis],target[axis]))+1,math.ceil(max(source[axis],target[axis]))):
            fraction=(coordinate-source[axis])/delta[axis]
            if 0<fraction<1:cuts.add(fraction)
    cuts=sorted(cuts);previous=None
    for a,b in zip(cuts,cuts[1:]):
        mid=[source[k]+delta[k]*(a+b)/2 for k in range(3)]
        x=math.floor(mid[0]);y=math.floor(mid[1]);index=y*64+x;t=tiles.get(index)
        why=None
        if t is None or t['type']==0:why='solid'
        elif index in door_tiles:why='closed_door_cell'
        elif not 1<=t['type']<=9:why='unsupported_complex'
        if why:return dict(cell=index,edge=[previous,index],reason=why)
        for fraction in (a,b):
            p=[source[k]+delta[k]*fraction for k in range(3)];u=p[0]-x;v=p[1]-y
            diagonal={2:u+v-1,3:v-u,4:1-u-v,5:u-v}.get(t['type'],1)
            if diagonal < -1e-7:return dict(cell=index,edge=[previous,index],reason='diagonal')
            zs=[]
            for face in ('floor','ceiling'):
                if t['type']<=5:zs.append(t[face]);continue
                # Interpolate north and south authoritative edge endpoints.
                north=h.edge_surface(t,0,face,u);south=h.edge_surface(t,2,face,1-u)
                zs.append(north*(1-v)+south*v)
            if not zs[0]-1e-7<=p[2]<=zs[1]+1e-7:return dict(cell=index,edge=[previous,index],reason='height')
        previous=index
    return None


def compile_profile(profile,solid_keys=None):
    tiles={t['id']:t for t in profile['diagnostics']['tiles']};solid_keys=solid_keys or {}
    portals=profile['diagnostics']['door_portals'];doors={i for p in portals for i in p['tiles']}
    records=[];counts=Counter();coverage=set();surface_coverage=set()
    for light in profile['lights']:
        bs,error=beams(light['position'],tiles,doors,light['receiver_tiles'])
        for i in light['receiver_tiles']:
            t=tiles[i];fs=faces(profile['level_id'],t,tiles,solid_keys)
            if not fs:
                for face in ('floor','ceiling'):
                    records.append(dict(surface_id=s.surface_id(profile['level_id'],t,face),light_id=light['id'],face=face,status='unsupported',reason='complex_tile',polygons=[],sample_pattern='unsupported',blocking_edges=[],portal_dependencies=[]))
                    counts['unsupported']+=1
            for face,sid,poly,axes in fs:
                status,pieces,paths,reason=receiver(poly,axes,bs.get(i,[]))
                if error:status='unsupported';reason=error
                origin=poly[0];base=hull([[p[k]-origin[k] for k in axes] for p in poly])
                # A deterministic 3x3 pattern of the COMPILED permission, not a
                # substitute for the geometric proof. '-' is outside the face.
                bounds=[(min(p[k] for p in base),max(p[k] for p in base)) for k in (0,1)]
                pattern='';witnesses=[]
                normal=cross(sub(poly[1],poly[0]),sub(poly[2],poly[0]));other=next(k for k in range(3) if k not in axes)
                for u in (.02,.5,.98):
                    for v in (.02,.5,.98):
                        q=[a+(b-a)*f for (a,b),f in zip(bounds,(u,v))]
                        inside=lambda ps:all(dot(p[:2],q)+p[2]>=0 for p in polygon_planes(ps))
                        point=origin[:]
                        for k,value in zip(axes,q):point[k]+=value
                        point[other]=origin[other]-sum(normal[k]*(point[k]-origin[k]) for k in axes)/normal[other]
                        witness=segment_witness(light['position'],point,tiles,doors) if inside(base) else None
                        pattern+=('-' if not inside(base) else 'B' if witness else 'V')
                        if witness and witness not in witnesses:witnesses.append(witness)
                blocking=[]
                for path in paths:
                    for a,b,edge in path:
                        if [a,b,edge] not in blocking:blocking.append([a,b,edge])
                # Retain all nearby closed portals whose excluded cell can bound
                # this beam box. They are diagnostic dependencies, never open.
                x0=min(light['position'][0],t['x']);x1=max(light['position'][0],t['x']+1)
                y0=min(light['position'][1],t['y']);y1=max(light['position'][1],t['y']+1)
                deps=[p['id'] for p in portals if any(x0-1<=tiles[j]['x']<=x1 and y0-1<=tiles[j]['y']<=y1 for j in p['tiles'] if j in tiles)]
                lit_polygons=[poly] if status=='full' else []
                for piece in pieces:
                    world=[]
                    for vertex in piece:
                        point=origin[:]
                        for k,value in zip(axes,vertex):point[k]+=value
                        point[other]=origin[other]-sum(normal[k]*(point[k]-origin[k]) for k in axes)/normal[other]
                        world.append(point)
                    lit_polygons.append(world)
                effective=light.get('intensity',1)>0 and any(distance_to_face(light['position'],p)<light.get('radius',math.inf)-GUARD for p in lit_polygons)
                records.append(dict(surface_id=sid,light_id=light['id'],face=face,status=status,reason=reason,
                    axes=list(axes),origin=origin,world_polygon=poly,polygons=pieces,sample_pattern=pattern,
                    blocking_edges=witnesses,beam_apertures=blocking,portal_dependencies=deps,
                    nonzero_falloff=effective,
                    boundary_planar=status=='partial',safe_split=status in ('full','partial'),
                    confidence='convex_cell_proof',generation_source='AUTHOR1E; unchanged AUTHOR1B sources'))
                counts[status]+=1
                if status in ('full','partial') and effective:coverage.add(i);surface_coverage.add(tuple(sid))
    records.sort(key=lambda r:(r['surface_id'],r['light_id']))
    local_counts=Counter(tuple(r['surface_id']) for r in records if r['status'] in ('full','partial'))
    if max(local_counts.values(),default=0)>MAX_LIGHTS:raise ValueError('Prior light budget exceeded')
    return dict(schema_version=1,level_id=profile['level_id'],baseline_signature=profile['baseline_signature'],rules_signature=profile['rules_signature'],
        model='convex-cell beam; types 1..9; all door cells closed; offline only',counts=dict(counts),
        covered_tiles=sorted(coverage),covered_surfaces=len(surface_coverage),records=records,
        bounds=dict(polygons=MAX_POLYGONS,vertices=MAX_VERTICES,lights=MAX_LIGHTS,beam_states=MAX_STATES,depth=MAX_DEPTH))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile',type=Path,required=True);parser.add_argument('--archive',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    profile=json.loads(args.profile.read_text());h.validate(profile)
    base=4000+100*profile['level_id'];res=h.ARCHIVE(args.archive.read_bytes(),base,base+100)
    # Bind raw solid-neighbor keys to the same owned archive as the profile.
    signature=h.digest(b''.join((base+i).to_bytes(2,'little')+res[base+i] for i in (2,4,5,7,8,19,20,22,42)))
    if signature!=profile['baseline_signature']:raise ValueError('Archive signature mismatch')
    raw=res[base+5];solid={}
    for i in range(4096):
        t=raw[i*16:i*16+16]
        if t[0]==0:solid[i]=(t[1]&31)<<6|(t[2]&31)<<11|t[3]<<16|((t[9]>>2)&3)<<24
    result=compile_profile(profile,solid);args.output.parent.mkdir(exist_ok=True,parents=True)
    args.output.write_bytes(h.encode(result));print(json.dumps({k:v for k,v in result.items() if k not in ('records','covered_tiles')}))


if __name__=='__main__':main()
