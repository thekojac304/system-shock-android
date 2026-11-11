"""Compile the accepted AUTHOR1D/1E data into bounded QA GLES assets.

The source JSON and owned archive stay under ignored dist/. The APK receives
only geometry keys, light parameters, receiver half-planes, and reviewed palette
index masks; it never receives original bitmap or map resources.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent))
import horror_author as author

ROOT = Path(__file__).resolve().parents[1]
MAGIC = b'HG1P'
VERSION = 1
MAX_LIGHTS = 4
MAX_POLYS = 2                 # accepted AUTHOR1E output on levels 1 and 6
MAX_PLANES = 8
MASK_IDS = (20, 26, 35, 36, 52, 82, 99, 101, 144, 155, 185, 188, 215, 216, 217)


def encode_level(level, archive, rules):
    profile_path = ROOT / f'dist/horror-author1d/generated/level-{level}.generated.json'
    partial_path = ROOT / f'dist/horror-author1e/level-{level}.partial.json'
    profile = json.loads(profile_path.read_text(encoding='utf-8'))
    partial = json.loads(partial_path.read_text(encoding='utf-8'))
    author.validate(profile)
    if (profile['level_id'] != level or partial['level_id'] != level or
            partial['schema_version'] != 1 or partial['model'] !=
            'convex-cell beam; types 1..9; all door cells closed; offline only' or
            partial['baseline_signature'] != profile['baseline_signature'] or
            partial['rules_signature'] != profile['rules_signature'] or
            profile['rules_signature'] != author.digest(author.encode(rules))):
        raise ValueError('Incompatible receiver/profile/rules signature or policy')
    base = 4000 + 100 * level
    resources = author.ARCHIVE(archive, base, base + 100)
    signature = author.digest(b''.join(struct.pack('<H', base + i) + resources[base + i]
                                       for i in (2, 4, 5, 7, 8, 19, 20, 22, 42)))
    if signature != profile['baseline_signature']:
        raise ValueError('Stale owned archive / map baseline')
    raw = resources[base + 5]
    if len(raw) != 4096 * 16:
        raise ValueError('Expected 64x64 map')
    mapkeys = []
    for i in range(4096):
        tile = raw[i * 16:(i + 1) * 16]
        mapkeys.append(tile[0] | ((tile[1] & 31) << 6) | ((tile[2] & 31) << 11) |
                       (tile[3] << 16) | (((tile[9] >> 2) & 3) << 24))

    regions = profile['regions']
    if len(regions) > 1024:
        raise ValueError('Region budget exceeded')
    region_lookup = [0] * 4096
    ambient = []
    default = profile['defaults']
    for region_number, region in enumerate(regions, 1):
        override = region['ambient_override']
        rgb = (override or default['ambient_rgb'])
        intensity = default['ambient_intensity']
        ambient.append(tuple(float(x) * intensity for x in rgb))
        for tile in region['tiles']:
            if not 0 <= tile < 4096 or region_lookup[tile]:
                raise ValueError('Overlapping/invalid region tile')
            region_lookup[tile] = region_number

    lights = profile['lights']
    if len(lights) > 512:
        raise ValueError('Light budget exceeded')
    light_lookup = {light['id']: i for i, light in enumerate(lights)}
    if len(light_lookup) != len(lights):
        raise ValueError('Duplicate light ID')
    portals = profile['diagnostics']['door_portals']
    if len(portals) > 64:
        raise ValueError('Portal dependency budget exceeded')
    portal_lookup = {p['id']: i for i, p in enumerate(portals)}

    surfaces = {}
    for rec in partial['records']:
        if rec['status'] not in ('full', 'partial') or not rec['nonzero_falloff']:
            continue
        if not rec['safe_split'] or rec['confidence'] != 'convex_cell_proof':
            raise ValueError('Uncertified receiver')
        sid = tuple(rec['surface_id'])
        if len(sid) != 9 or sid[0] != level:
            raise ValueError('Invalid SurfaceId')
        polygons = rec['polygons'] if rec['status'] == 'partial' else []
        if (rec['status'] == 'partial' and not polygons or
                len(polygons) > MAX_POLYS or
                any(not 3 <= len(poly) <= MAX_PLANES for poly in polygons)):
            raise ValueError('Partial polygon exceeds accepted runtime bound')
        planes = []
        if polygons:
            a, b = rec['axes']
            origin = rec['origin']
            for poly in polygons:
                # AUTHOR1E polygons are already inward guarded. Converting to
                # world-space planes removes face-axis/origin work from GLES.
                out = []
                for p, q in zip(poly, poly[1:] + poly[:1]):
                    dx, dy = q[0] - p[0], q[1] - p[1]
                    length = math.hypot(dx, dy)
                    if length < 1e-9:
                        raise ValueError('Degenerate partial edge')
                    na, nb = -dy / length, dx / length
                    normal = [0.0, 0.0, 0.0]
                    normal[a], normal[b] = na, nb
                    d = -(na * (origin[a] + p[0]) + nb * (origin[b] + p[1])) - 1e-5
                    out.append((*normal, d))
                planes.append(out)
        deps = 0
        for portal in rec['portal_dependencies']:
            deps |= 1 << portal_lookup[portal]
        surfaces.setdefault(sid, []).append((light_lookup[rec['light_id']],
                                               2 if polygons else 1, planes, deps))
    if any(len(rels) > MAX_LIGHTS for rels in surfaces.values()):
        raise ValueError('Per-surface light budget exceeded')

    body = bytearray()
    body += struct.pack('<4096I', *mapkeys)
    body += struct.pack('<4096H', *region_lookup)
    body += struct.pack('<3f', *(float(x) * default['ambient_intensity']
                                  for x in default['ambient_rgb']))
    for rgb in ambient:
        body += struct.pack('<3f', *rgb)
    for light in lights:
        body += struct.pack('<8fii', *light['position'], *light['rgb'],
                            light['intensity'], light['radius'],
                            light['source_resource_id'] or -1,
                            light['source_object_id'] if light['source_object_id'] is not None else -1)
    for sid, rels in sorted(surfaces.items()):
        body += struct.pack('<8BII B', *sid[:7], 1, sid[7], sid[8], len(rels))
        for index, kind, polygons, deps in sorted(rels):
            body += struct.pack('<HBBQ', index, kind, len(polygons), deps)
            for poly in polygons:
                body += struct.pack('<B', len(poly))
                for plane in poly:
                    body += struct.pack('<4f', *plane)
    masks = rules['reviewed_emissive_indices']
    if tuple(sorted(int(i) for i in masks)) != tuple(sorted(MASK_IDS)):
        raise ValueError('Reviewed mask inventory changed')
    active_masks = set(rules['classes']['luminous_fixture']) | set(
        rules.get('deck_luminous_fixture', {}).get(str(level), []))
    for material in MASK_IDS:
        bits = bytearray(256)
        for index in masks[str(material)] if material in active_masks else ():
            if not 0 <= index <= 255:
                raise ValueError('Invalid emissive palette index')
            bits[index] = 1
        body += bits

    header = struct.pack('<4s6I32s32s', MAGIC, VERSION, level, len(lights),
                         len(surfaces), len(regions), zlib.crc32(body),
                         bytes.fromhex(profile['baseline_signature']),
                         bytes.fromhex(profile['rules_signature']))
    return header + body, dict(level=level, lights=len(lights), surfaces=len(surfaces),
                               regions=len(regions), bytes=len(header) + len(body),
                               sha256=hashlib.sha256(header + body).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, default=ROOT / 'dist/audio2/owned-ARCHIVE.DAT')
    parser.add_argument('--output', type=Path, default=ROOT / 'AndroidProject/app/src/qa/assets/horror-generated')
    args = parser.parse_args()
    archive = args.archive.read_bytes()
    rules = json.loads((ROOT / 'profiles/horror-author1/rules.json').read_text(encoding='utf-8'))
    levels = (0, 1, 2, 4, 5, 6, 7, 9)
    results = [encode_level(level, archive, rules) for level in levels]
    args.output.mkdir(parents=True, exist_ok=True)
    for old in args.output.glob('level-*.bin'):
        if old.stem.removeprefix('level-').isdigit() and int(old.stem[6:]) not in levels:
            old.unlink()
    for (blob, summary), level in zip(results, levels):
        (args.output / f'level-{level}.bin').write_bytes(blob)
        print(json.dumps(summary, sort_keys=True))


if __name__ == '__main__':
    main()
