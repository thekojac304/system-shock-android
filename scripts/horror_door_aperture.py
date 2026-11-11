"""AUTHOR1E frame-indexed bitmap aperture audit, using actual owned door art.

This is a local-plane silhouette model, NOT a certified terrain portal model.
The largest empty texel rectangle is an inscribed conservative aperture; no
physics-frame threshold is used. Frame 0 always blocks generated crossing.
"""
import argparse
import json
from pathlib import Path
import struct
import math
import horror_author as h


def decode_rsd(data,size):
    out=bytearray();pos=0;ended=False
    while pos<len(data):
        code=data[pos];pos+=1
        if code==128:
            if pos+2>len(data):raise ValueError('Truncated RSD opcode')
            code=int.from_bytes(data[pos:pos+2],'little');pos+=2
            if code==0:ended=True;break
            if code<32768:mode='skip';count=code
            elif code<49152:mode='copy';count=code&32767
            else:mode='run';count=code&16383
        elif code==0:
            if pos>=len(data):raise ValueError('Truncated RSD run')
            count=data[pos];pos+=1;mode='run'
        elif code<128:count=code;mode='copy'
        else:count=code&127;mode='skip'
        if len(out)+count>size:raise ValueError('RSD overflow')
        n=count if mode=='copy' else 1 if mode=='run' else 0
        if pos+n>len(data):raise ValueError('Truncated RSD data')
        out.extend(data[pos:pos+count] if mode=='copy' else bytes([data[pos]])*count if mode=='run' else bytes(count))
        pos+=n
    if not ended:raise ValueError('Missing RSD terminator')
    out.extend(bytes(size-len(out))) # rsdcvt.c pads unwritten pixels with zero.
    return bytes(out)


def frames(chunk):
    n=int.from_bytes(chunk[:2],'little');header=2+4*(n+1)
    if not 1<=n<=32 or len(chunk)<header:raise ValueError('Unsupported frame count')
    offsets=struct.unpack_from('<'+'I'*(n+1),chunk,2)
    if offsets[0]<header or offsets[-1]>len(chunk) or list(offsets)!=sorted(offsets):raise ValueError('Bad frame offsets')
    result=[]
    for start,end in zip(offsets,offsets[1:]):
        frame=chunk[start:end]
        if len(frame)<28:raise ValueError('Short FrameDesc')
        kind=frame[4];flags=int.from_bytes(frame[6:8],'little');w,ht,row=struct.unpack_from('<HHH',frame,8)
        if not 0<w<=256 or not 0<ht<=256 or row!=w:raise ValueError('Unsupported bitmap stride/dimensions')
        if kind==4:pixels=decode_rsd(frame[28:],w*ht)
        elif kind==2:
            pixels=frame[28:28+w*ht]
            if len(pixels)!=w*ht:raise ValueError('Short flat bitmap')
        else:raise ValueError('Unsupported bitmap type')
        result.append(dict(width=w,height=ht,flags=flags,pixels=pixels))
    return result


def empty_rectangle(pixels,w,ht):
    """Largest all-zero rectangle, deterministic histogram stack, half-open."""
    heights=[0]*w;best=(0,0,0,0);best_area=0
    for y in range(ht):
        for x in range(w):heights[x]=heights[x]+1 if pixels[y*w+x]==0 else 0
        stack=[]
        for x in range(w+1):
            height=heights[x] if x<w else 0;start=x
            while stack and stack[-1][1]>height:
                left,hh=stack.pop();candidate=(left,y+1-hh,x,y+1);a=(x-left)*hh
                if a>best_area or a==best_area and candidate<best:best=candidate;best_area=a
                start=left
            if not stack or stack[-1][1]<height:stack.append((start,height))
    return list(best) if best_area else None


def aperture(frame,index):
    w=frame['width'];ht=frame['height']
    if index==0 or not frame['flags']&1:return None
    rect=empty_rectangle(frame['pixels'],w,ht)
    if rect is None:return None
    # One complete texel inward guards nearest-texel boundary rounding. The
    # original renderer half-extents are w/128 and h/128 (gameobj scale=0).
    x0,y0,x1,y1=rect;x0+=1;y0+=1;x1-=1;y1-=1
    if x1<=x0 or y1<=y0:return None
    return [(x0-w/2)/64,(ht/2-y1)/64,(x1-w/2)/64,(ht/2-y0)/64]


def resource_id(obj):
    return 2400+sum((10,9,7,5,10)[:obj['subclass']])+obj['type']


def changed_receivers(dependencies,old_frames,new_frames,portals):
    """Reference invalidation: a frame change dirties only dependent records."""
    changed={p['id'] for p in portals if any(old_frames.get(i)!=new_frames.get(i) for i in p['object_ids'])}
    return [i for i,ids in enumerate(dependencies) if changed.intersection(ids)]


def paired_aperture(portal,objects,models,frame_state):
    """Return per-panel local apertures. Missing/closed half disables crossing.

    These must be projected at EACH actual object plane before intersecting a
    terrain beam. Intersecting at the collision edge is NOT an approved shortcut.
    """
    panels=[]
    for oid in portal['object_ids']:
        obj=objects.get(oid);index=frame_state.get(oid)
        if obj is None or not isinstance(index,int):return []
        model=models.get(resource_id(obj))
        if model is None or not 0<=index<len(model):return []
        ap=model[index]['aperture']
        if ap is None:return []
        panels.append(dict(object_id=oid,frame=index,local_rectangle=ap,position=obj['position'],heading=obj['heading']))
    return panels


def audit(profiles,resources):
    used={resource_id(o) for p in profiles for o in p['diagnostics']['objects'] if 'door' in o}
    models={};checks=0
    for rid in sorted(used):
        rows=[]
        for index,f in enumerate(frames(resources[rid])):
            ap=aperture(f,index)
            # Exhaustively validate the continuous rectangle against every
            # intersected texel cell, not just the rectangle corner samples.
            if ap:
                x0,y0,x1,y1=ap;w=f['width'];ht=f['height']
                for y in range(math.floor(ht/2-y1*64),math.ceil(ht/2-y0*64)):
                    for x in range(math.floor(x0*64+w/2),math.ceil(x1*64+w/2)):
                        if f['pixels'][y*w+x]!=0:raise ValueError('Opaque texel in aperture')
                        checks+=1
            rows.append(dict(frame=index,width=f['width'],height=f['height'],transparent_pixels=f['pixels'].count(0),
                aperture=ap,pixel_signature=h.digest(f['pixels']),panel_half_extents=[f['width']/128,f['height']/128]))
        models[rid]=rows
    discontinuities=[]
    for rid,rows in models.items():
        for previous,current in zip(rows,rows[1:]):
            a=previous['aperture'];b=current['aperture']
            if not a or not b:continue
            overlap=max(0,min(a[2],b[2])-max(a[0],b[0]))*max(0,min(a[3],b[3])-max(a[1],b[1]))
            lost=1-overlap/((a[2]-a[0])*(a[3]-a[1]))
            if lost>1e-6:discontinuities.append(dict(resource_id=rid,from_frame=previous['frame'],to_frame=current['frame'],lost_previous_fraction=lost))
    levels={}
    for p in profiles:
        objects={o['id']:o for o in p['diagnostics']['objects']};portals=p['diagnostics']['door_portals']
        rows=[]
        for portal in portals:
            obj=objects[portal['object_id']];rid=resource_id(obj)
            # gameobj.c snaps both XY raw fractions ff00 to the next tile.
            rendered_xy=[round(v) if round(v*256)%256==255 else v for v in obj['position'][:2]]
            rows.append(dict(portal_id=portal['id'],resource_id=rid,object_ids=portal['object_ids'],heading=obj['heading'],
                position=obj['position'],rendered_xy=rendered_xy,collision_plane=portal['plane'],edge_plane=portal['edge_plane'],
                supported_orientation=obj['heading'] in (0,64,128,192) and obj['pitch']==obj['bank']==0,
                frames=len(models[rid]),paired=bool(portal['other_half']),
                limitation='Local silhouette certified; source-dependent projection at rendered plane and paired composition not certified'))
        levels[p['level_id']]=rows
    return dict(model='discrete bitmap frame; fixed center/orientation; no continuous panel motion',
        texel_checks=checks,resources=models,levels=levels,non_nested_aperture_transitions=discontinuities,
        local_silhouette_gate='PASS',animated_portal_gate='FAIL',
        gate='FAIL: independent largest rectangles relocate during opening; world-space and paired-half projection not certified; no crossing permissions emitted')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profiles',nargs='+',type=Path,required=True);parser.add_argument('--art',nargs='+',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args();resources={}
    for p in args.art:resources.update(h.ARCHIVE(p.read_bytes(),2400,2441))
    result=audit([json.loads(p.read_text()) for p in args.profiles],resources)
    result['input_signatures']={str(p):h.digest(p.read_bytes()) for p in args.art}
    args.output.write_bytes(h.encode(result));print(result['gate']);print('Validated aperture texels:',result['texel_checks'])
    raise SystemExit(2) # A completed audit is not a passing animated-portal gate.


if __name__=='__main__':main()
