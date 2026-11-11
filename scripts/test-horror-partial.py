"""Post-compilation AUTHOR1E audit. The held-out reference is review only."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import horror_partial as p
import horror_author as h


def audit(profile,compiled):
    tiles={t['id']:t for t in profile['diagnostics']['tiles']}
    doors={i for portal in profile['diagnostics']['door_portals'] for i in portal['tiles']}
    lights={light['id']:light for light in profile['lights']}
    count=0;leaks=[];taxonomy=Counter();complexity=[];samples=Counter();patterns=Counter();examples={}
    for r in compiled['records']:
        taxonomy[r['face']+'_'+r['status']]+=1;samples[r['sample_pattern']]+=1
        if r['status']=='partial':
            reasons=sorted({w['reason'] for w in r['blocking_edges']})
            pattern='+'.join(reasons) or 'subsample_boundary'
            patterns[pattern]+=1
            examples.setdefault(pattern,[])
            if len(examples[pattern])<3:examples[pattern].append({k:r[k] for k in ('surface_id','light_id','face','sample_pattern','blocking_edges','beam_apertures','polygons','portal_dependencies')})
        if r['status'] not in ('full','partial'):continue
        origin=r['origin'];axes=r['axes'];poly=r['world_polygon']
        normal=p.cross(p.sub(poly[1],poly[0]),p.sub(poly[2],poly[0]));other=next(k for k in range(3) if k not in axes)
        polys=r['polygons'] if r['status']=='partial' else [p.hull([[v[k]-origin[k] for k in axes] for v in poly])]
        if r['status']=='partial':complexity.extend(len(v) for v in polys)
        for polygon in polys:
            center=p.mean(polygon)
            probes=[center]
            for a,b in zip(polygon,polygon[1:]+polygon[:1]):
                for edge_fraction in (0,.25,.5,.75):
                    edge=[a[k]+(b[k]-a[k])*edge_fraction for k in (0,1)]
                    for inward in (1e-5,.05,.3,.7):
                        probes.append([edge[k]+(center[k]-edge[k])*inward for k in (0,1)])
            for q in probes:
                point=origin[:]
                for k,v in zip(axes,q):point[k]+=v
                point[other]=origin[other]-sum(normal[k]*(point[k]-origin[k]) for k in axes)/normal[other]
                witness=p.segment_witness(lights[r['light_id']]['position'],point,tiles,doors);count+=1
                if witness:leaks.append(dict(surface_id=r['surface_id'],light_id=r['light_id'],point=point,witness=witness))
    covered=set(compiled['covered_tiles']);dark={i for i,t in tiles.items() if min(t['darkness'])>=10}
    result=dict(tile_coverage=len(covered),proposed_tiles=len({i for l in lights.values() for i in l['receiver_tiles']}),
        surface_coverage=compiled['covered_surfaces'],counts=compiled['counts'],taxonomy=dict(sorted(taxonomy.items())),
        sample_patterns=dict(samples),checked_points=count,hidden_fragment_failures=len(leaks),first_failures=leaks[:20],
        polygon_vertices_average=sum(complexity)/len(complexity) if complexity else 0,polygon_vertices_max=max(complexity,default=0),
        partial_polygon_count=len(complexity),dark_tiles_without_receiver=len(dark-covered),
        recurring_partial_patterns=dict(patterns),partial_examples=examples,
        max_polygons=max((len(r['polygons']) for r in compiled['records']),default=0),
        max_lights_per_surface=max(Counter(tuple(r['surface_id']) for r in compiled['records'] if r['status'] in ('full','partial')).values(),default=0))
    if profile['level_id']==1:
        text=(h.ROOT/'SourceSnapshot/src/GameSrc/horror_profile.c').read_text().split('const HvTile hv_medical_tiles[] = {',1)[1].split('};',1)[0]
        reference={int(y)*64+int(x) for x,y in re.findall(r'\{(\d+),(\d+),',text)}
        rows=[r for r in compiled['records'] if r['surface_id'][2]*64+r['surface_id'][1] in reference]
        result['holdout']=dict(covered_tiles=sorted(reference&covered),tile_total=len(reference),
            lit_surfaces=len({tuple(r['surface_id']) for r in rows if r.get('nonzero_falloff')}),
            candidate_surfaces=len({tuple(r['surface_id']) for r in rows}),relationships=dict(Counter(r['status'] for r in rows)))
    else:
        result['chandeliers']={l['source_object_id']:sum(r['light_id']==l['id'] and r.get('nonzero_falloff',False) for r in compiled['records'])
            for l in lights.values() if l['source_object_id'] in (62,63,69,70,180,181)}
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profiles',type=Path,required=True);parser.add_argument('--compiled',type=Path,required=True)
    args=parser.parse_args();results={}
    for level in (1,6):
        profile=json.loads((args.profiles/f'level-{level}.generated.json').read_text())
        compiled=json.loads((args.compiled/f'level-{level}.partial.json').read_text())
        results[level]=audit(profile,compiled)
    (args.compiled/'receiver-audit.json').write_bytes(h.encode(results));print(json.dumps(results,indent=2))
    if any(r['hidden_fragment_failures'] for r in results.values()):raise SystemExit(1)


if __name__=='__main__':main()
