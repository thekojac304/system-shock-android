"""Read-only deck source/receiver census using the frozen AUTHOR1 rules.

The accepted pipeline is tried first. A temporary archetype copy then permits
diagnostic source/receiver counting on the seven decks without an archetype;
this copy is never compiled, packaged, or written back to rules.json.
Owned inputs and detailed output remain in ignored dist/.
"""
from collections import Counter
import copy
import hashlib
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
import horror_author as h
import horror_partial as partial

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "dist/gpu-grove1-decklight-diag1"
DECKS = {0: "Reactor", 2: "Research", 3: "Maintenance", 4: "Storage",
         5: "Flight Deck", 7: "Engineering", 8: "Security", 9: "Bridge"}
BASELINE = {20, 35, 36, 82, 101, 144, 185, 215, 216, 217}


def surfaces_in_raw(res, level):
    """Count mapped IDs even when the strict loader rejects a texture slot."""
    base = 4000 + 100 * level
    ids = [v[0] for v in __import__('struct').iter_unpack('<H', res[base + 7])]
    raw = res[base + 5]
    counts, invalid = Counter(), []
    for tile in range(4096):
        t = raw[tile * 16:tile * 16 + 16]
        if not t or not t[0]:
            continue
        packed = int.from_bytes(t[6:8], 'little')
        for face, slot in (("wall", packed & 63), ("floor", packed >> 11),
                           ("ceiling", (packed >> 6) & 31)):
            if slot >= len(ids):
                invalid.append((tile, face, slot, "slot-out-of-range"))
            else:
                counts[(face, ids[slot])] += 1
    return counts, invalid


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    archive = (ROOT / 'dist/audio2/owned-ARCHIVE.DAT').read_bytes()
    textprop = ROOT / 'dist/gpu-render2-3-4/TEXTPROP.DAT'
    objprop = ROOT / 'dist/gpu-render2-3-4/owned-objprop.dat'
    rules_path = ROOT / 'profiles/horror-author1/rules.json'
    rules_raw = rules_path.read_bytes()
    rules = json.loads(rules_raw)
    assert set(rules['classes']['luminous_fixture']) == BASELINE
    props = h.PROPS(textprop)
    objects = h.object_properties(objprop)
    resource = h.ARCHIVE(archive, 4000, 5000)
    result = {"method": ("Frozen source generation attempted first. A separate, in-memory "
              "Medical ambient archetype and bypass of the two-level schema gate permit "
              "source/receiver census only; source, mask, confidence, and geometry rules "
              "remain frozen. No diagnostic profile is a valid runtime asset."),
              "input_sha256": {"archive": hashlib.sha256(archive).hexdigest(),
              "textprop": hashlib.sha256(textprop.read_bytes()).hexdigest(),
              "objprop": hashlib.sha256(objprop.read_bytes()).hexdigest(),
              "rules": hashlib.sha256(rules_raw).hexdigest()}, "decks": {}}
    for level, name in DECKS.items():
        row = {"level": level, "name": name}
        count, invalid = surfaces_in_raw(resource, level)
        row['mapped_materials'] = {str(i): {face: count[face, i] for face in ('wall', 'floor', 'ceiling')
                                   if count[face, i]} for i in sorted({i for _, i in count})}
        row['invalid_texture_slots'] = invalid
        row['reviewed_fixture_occurrences'] = sum(n for (face, i), n in count.items() if i in BASELINE)
        row['reviewed_fixture_ids'] = sorted({i for _, i in count if i in BASELINE})
        row['unreviewed_material_ids'] = sorted({i for _, i in count if i not in BASELINE})
        try:
            data = h.load_level(resource, level, props)
            try:
                h.generate(data, props, rules, objects)
                row['frozen_first_failure'] = 'unexpected source profile success'
            except Exception as exc:
                row['frozen_first_failure'] = f'{type(exc).__name__}: {exc}'
            # Only an ambient archetype entry is copied for this diagnostic.
            # All source classes, masks, confidence and geometry rules stay fixed.
            diagnostic_rules = copy.deepcopy(rules)
            diagnostic_rules['archetypes'][str(level)] = copy.deepcopy(rules['archetypes']['1'])
            # The accepted schema admits only levels 1 and 6. Skip that final
            # level gate for this in-memory census; the frozen attempt above
            # records its own actual failure and remains authoritative.
            accepted_validate = h.validate
            try:
                h.validate = lambda _profile: None
                profile = h.generate(data, props, diagnostic_rules, objects)
            finally:
                h.validate = accepted_validate
            candidates = profile['diagnostics']['candidates']
            row['source_counts'] = {"candidate": len(candidates),
                                    "accepted": len(profile['lights']),
                                    "policy": dict(sorted(Counter(c['policy'] for c in candidates).items())),
                                    "object_candidates": sum(c['face'] == 'object' for c in candidates)}
            row['geometry'] = {"tiles": len(data['tiles']),
                               "supported_tile_types_1_to_9": sum(1 <= t['type'] <= 9 for t in data['tiles'].values()),
                               "unsupported_tile_types": dict(sorted(Counter(t['type'] for t in data['tiles'].values()
                                                                        if not 1 <= t['type'] <= 9).items()))}
            solid = {}
            raw = resource[4000 + 100 * level + 5]
            for i in range(4096):
                t = raw[i * 16:i * 16 + 16]
                if t[0] == 0:
                    solid[i] = (t[1] & 31) << 6 | (t[2] & 31) << 11 | t[3] << 16 | ((t[9] >> 2) & 3) << 24
            try:
                receiver = partial.compile_profile(profile, solid)
                row['receiver_counts'] = receiver['counts']
                row['receiver_covered_surfaces'] = receiver['covered_surfaces']
                row['receiver_covered_tiles'] = len(receiver['covered_tiles'])
                row['receiver_failure_reasons'] = dict(sorted(Counter(r.get('reason') for r in receiver['records']
                    if r['status'] == 'unsupported').items(), key=lambda p: str(p[0])))
                status_by_surface = {}
                for record in receiver['records']:
                    key = tuple(record['surface_id'])
                    status_by_surface.setdefault(key, set()).add(record['status'])
                row['receiver_surface_counts'] = {
                    'enumerated': len(status_by_surface),
                    'full_or_partial': sum(bool(statuses & {'full', 'partial'})
                                           for statuses in status_by_surface.values()),
                    'unsupported_only': sum(statuses == {'unsupported'}
                                            for statuses in status_by_surface.values()),
                    'blocked_only': sum(statuses == {'blocked'}
                                        for statuses in status_by_surface.values()),
                }
                row['over_budget_relationships'] = 0
                row['complexity_bound_failures'] = sum(n for reason, n in row['receiver_failure_reasons'].items()
                                                       if reason in ('beam_complexity', 'polygon_complexity'))
            except Exception as exc:
                row['receiver_first_failure'] = f'{type(exc).__name__}: {exc}'
        except Exception as exc:
            row['frozen_first_failure'] = f'{type(exc).__name__}: {exc}'
            row['source_counts'] = None
            row['receiver_counts'] = None
        result['decks'][name] = row
        print(name, row['frozen_first_failure'], row.get('source_counts'), row.get('receiver_counts'), flush=True)
    assert rules_path.read_bytes() == rules_raw, 'Frozen rules changed during diagnosis'
    (OUT / 'decklight-diagnostics.json').write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')


if __name__ == '__main__':
    main()
