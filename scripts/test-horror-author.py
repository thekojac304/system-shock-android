"""Repeatable two-level diagnostic gate; no ADB, renderer changes or save writes.

Use --archive, --textprop, --objprop and --output dist/horror-author1.
Generated proprietary-derived diagnostics stay in the selected local output.
"""
import argparse
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import time
sys.dont_write_bytecode=True
import horror_author as h


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('archive','textprop','objprop','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--texture',type=Path,help='Owned TEXTURE.RES for AUTHOR1C reviewed-mask verification')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    paths=[args.archive.resolve(),args.textprop.resolve(),args.objprop.resolve()]
    if args.texture:paths.append(args.texture.resolve())
    before={str(p):h.digest(p.read_bytes()) for p in paths}
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    tests=subprocess.run([sys.executable,'-m','unittest','discover','-s','tests/visuals','-p','test_horror_author.py','-v'],
        cwd=h.ROOT,env=env,capture_output=True,text=True)
    (out/'unit-tests.log').write_text(tests.stdout+tests.stderr,encoding='utf-8');tests.check_returncode()
    res=h.ARCHIVE(paths[0].read_bytes(),4000,5400);props=h.PROPS(paths[1]);objprops=h.object_properties(paths[2]);rules=json.loads(h.RULES.read_text())
    mask_audit=h.emissive_mask_audit(paths[3].read_bytes(),rules) if args.texture else None
    basecmd=[sys.executable,str(h.ROOT/'scripts/horror_author.py'),'--archive',str(paths[0]),
             '--textprop',str(paths[1]),'--objprop',str(paths[2])]
    generation=subprocess.run(basecmd+['--output',str(out/'generated')],env=env,capture_output=True,text=True,check=True)
    (out/'generation.log').write_text(generation.stdout,encoding='utf-8');results={}
    for level in (1,6):
        start=time.perf_counter();a=h.generate(h.load_level(res,level,props),props,rules,objprops);elapsed=time.perf_counter()-start
        b=h.generate(h.load_level(res,level,props),props,rules,objprops)
        if h.encode(a)!=h.encode(b):raise ValueError('Nondeterministic generation')
        replacement=copy.deepcopy(a['lights'][0]);replacement['intensity']=.375
        overlay=dict(schema_version=1,baseline_signature=a['baseline_signature'],patches=[dict(collection='lights',id=replacement['id'],replacement=replacement)])
        # Test-only override, never an art-direction input or delivered profile.
        overlay_path=out/f'level-{level}.manual-test.json';overlay_path.write_bytes(h.encode(overlay));manual_hash=h.digest(overlay_path.read_bytes())
        cmd=basecmd+['--levels',str(level),'--output',str(out/'regenerated'),'--overlay',str(overlay_path)]
        for _ in range(2):subprocess.run(cmd,env=env,capture_output=True,check=True)
        if h.digest(overlay_path.read_bytes())!=manual_hash:raise ValueError('Manual overlay modified')
        merged=json.loads((out/'regenerated'/f'level-{level}.merged.json').read_text())
        if merged['lights'][0]['intensity']!=.375:raise ValueError('Manual override lost')
        if (out/'regenerated'/f'level-{level}.generated.json').read_bytes()!=(out/'generated'/f'level-{level}.generated.json').read_bytes():raise ValueError('CLI regeneration differs')
        ts={t['id']:t for t in a['diagnostics']['tiles']};members={i:t['region'] for i,t in ts.items()}
        _,blocked,ambiguous=h.door_portals(h.load_level(res,level,props))
        for light in a['lights']:
            for i in light['receiver_tiles']:
                t=ts[i]
                if not any(h.visible_receiver(light['position'],[t['x']+sx,t['y']+sy,
                    (h.surface_height(t,'floor')+h.surface_height(t,'ceiling'))/2],
                    ts,members,light['receiver_regions'][0],ambiguous,blocked)
                    for sx,sy in ((.5,.5),(.25,.5),(.75,.5),(.5,.25),(.5,.75))):
                    raise ValueError('Invalid receiver sample')
        results[level]=dict(reproducible=True,manual_overlay_unchanged=True,merged_override_preserved=True,
            receiver_sample_checks=True,receiver_face_audit=h.receiver_face_audit(a),
            generation_seconds=round(elapsed,4),baseline_signature=a['baseline_signature'])
    if before!={str(p):h.digest(p.read_bytes()) for p in paths}:raise ValueError('Source inputs changed')
    manifest=dict(gate='FAIL: AUTHOR1C receiver safety; offline regeneration passed; reviewed masks '+
        ('verified' if mask_audit is not None else 'not verified (no TEXTURE.RES supplied)'),
        emissive_mask_audit=mask_audit,inputs=before,results=results,
        artifacts={str(p.relative_to(out)):h.digest(p.read_bytes()) for p in sorted((out/'generated').glob('*'))})
    (out/'diagnostic-gate.json').write_bytes(h.encode(manifest));print(json.dumps(manifest,indent=2))


if __name__=='__main__':main()
