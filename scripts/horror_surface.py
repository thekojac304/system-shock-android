"""Conservative AUTHOR1D terrain receiver compiler for two generated levels.

Only an unobstructed rectangular prism of ordinary flat tiles certifies a
whole floor/ceiling face. Everything else remains an explicit unsupported
receiver; the diagnostic tile mask is never promoted to a runtime allowance.
"""
import argparse
import json
import math
from pathlib import Path
import horror_author


def geometry_key(tile):
    shift=tile['height_shift']
    floor=round(tile['floor']*(1<<shift))
    ceiling=32-round(tile['ceiling']*(1<<shift))
    if not all(0<=v<=31 for v in (floor,ceiling)):
        raise ValueError('Unrepresentable map height')
    return tile['type'] | floor<<6 | ceiling<<11 | tile['param']<<16 | tile['mirror']<<24


def surface_id(level,tile,face):
    return [level,tile['x'],tile['y'],1 if face=='floor' else 2,0,0,
            tile['type'],geometry_key(tile),0]


def full_face_prism(source,target,face,tiles,closed_door_tiles):
    """Sufficient whole-face visibility proof, deliberately not a sampler.

    The union of segments from one source to every point of the target square
    lies inside its axis-aligned XY bounding box and between the two endpoint
    heights. Requiring every cell in that box to be a flat, open, common
    vertical slab rules out hidden terrain fragments. Boundary/door cells
    are excluded even where a narrower cone might have passed beside them.
    """
    if target['type']!=1 or len(source)!=3:
        return False
    z=target[face]+(.02 if face=='floor' else -.02)
    lo=min(source[2],z);hi=max(source[2],z)
    x0=math.floor(min(source[0],target['x']))
    x1=math.ceil(max(source[0],target['x']+1))-1
    y0=math.floor(min(source[1],target['y']))
    y1=math.ceil(max(source[1],target['y']+1))-1
    for y in range(y0,y1+1):
        for x in range(x0,x1+1):
            tile=tiles.get(y*64+x)
            if (tile is None or tile['type']!=1 or tile['id'] in closed_door_tiles or
                    tile['floor']+.01>=lo or tile['ceiling']-.01<=hi):
                return False
    return True


def compile_surface_receivers(profile):
    tiles={t['id']:t for t in profile['diagnostics']['tiles']}
    # Until bitmap aperture geometry is certified, every door cell fails shut.
    doors={i for p in profile['diagnostics']['door_portals'] for i in p['tiles']}
    records=[];counts={'full':0,'rejected':0,'complex':0};covered=set()
    for light in profile['lights']:
        for index in light['receiver_tiles']:
            tile=tiles[index]
            for face in ('floor','ceiling'):
                if tile['type']!=1:
                    counts['complex']+=1
                elif full_face_prism(light['position'],tile,face,tiles,doors):
                    records.append({'surface_id':surface_id(profile['level_id'],tile,face),
                                    'light_id':light['id']})
                    covered.add(index);counts['full']+=1
                else:
                    counts['rejected']+=1
    records.sort(key=lambda r:(r['surface_id'],r['light_id']))
    return {'schema_version':1,'level_id':profile['level_id'],
            'baseline_signature':profile['baseline_signature'],
            'rules_signature':profile['rules_signature'],
            'model':'flat-prism-full-face-only; all doors closed',
            'counts':counts,'covered_tiles':len(covered),'records':records}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    profile=json.loads(args.profile.read_text(encoding='utf-8'))
    horror_author.validate(profile)
    result=compile_surface_receivers(profile)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2,sort_keys=True)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='records'},sort_keys=True))


if __name__=='__main__':
    main()
