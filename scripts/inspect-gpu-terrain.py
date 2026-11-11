"""Read owned System Shock archives; print terrain metadata and QA route hints.

Usage: python scripts/inspect-gpu-terrain.py ARCHIVE.DAT [savgam*.dat ...]
       [--textprop path/to/TEXTPROP.DAT] [--json] [--examples 3]

Reads files only. No ADB, game-state writes, proprietary resource extraction, or
render validation. Shell wildcards are expanded locally. Coordinate candidates
are geometric hints, not navigation paths or proof that a door is accessible.
"""
import argparse
from collections import Counter
import glob
import json
from pathlib import Path
import re
import runpy
import struct


ROOT = Path(__file__).resolve().parents[1]
# Share the existing 14-bit resource decompressor and archive-directory parser.
RESOURCES = runpy.run_path(str(ROOT / "scripts/inspect-vis1-map.py"))["resources"]
NAMES = {int(value): name for name, value in re.findall(
    r"^#define\s+(TILE_\w+)\s+(\d+)\s*$",
    (ROOT / "SourceSnapshot/src/GameSrc/Headers/tilename.h").read_text(), re.M)}
DECKS = ["Reactor", "Medical", "Research", "Maintenance", "Storage",
         "Flight", "Executive", "Engineering", "Security", "Bridge"]


def texture_properties(path):
    if path is None:
        return None
    raw = path.read_bytes()
    # textmaps.c load_master_texture_properties: version 9, 363 11-byte records.
    if len(raw) < 4 + 363 * 11 or struct.unpack_from("<I", raw)[0] != 9:
        raise ValueError("Unsupported/truncated TEXTPROP.DAT; expected version 9")
    return [dict(family_texture=raw[4+i*11], force_dir=raw[12+i*11],
                 anim_group=raw[13+i*11], group_pos=raw[14+i*11])
            for i in range(363)]


def object_location(raw, version, obj_id):
    # archiveformat.c ObjV11Layout / ObjV12Layout.
    pad = int(version >= 12)
    size = 27 + pad
    if len(raw) % size or not 0 <= obj_id < len(raw) // size:
        return None
    obj = raw[obj_id*size:(obj_id+1)*size]
    if not obj[0]:
        return None
    x, y = struct.unpack_from("<HH", obj, 11 + pad)
    return dict(x=x/256, y=y/256, z_raw=obj[15+pad],
                pitch=obj[16+pad], heading=obj[17+pad], bank=obj[18+pad])


def player_metadata(res):
    p = res.get(4001)
    if p is None:
        return None
    # playerlayout.h prefix is identical in M32 (1397) and M64 (1429).
    if len(p) not in (1397, 1429):
        return dict(error="Unsupported player resource size", size=len(p))
    level, rep = p[57], struct.unpack_from("<H", p, 108)[0]
    x, y = struct.unpack_from("<HH", p, 110)
    base = 4000 + level * 100
    version = int.from_bytes(res.get(base+2, b"\0"), "little")
    # gamewrap.c:215 copies the current object location here when saving;
    # gamewrap.c:352 restores it. The player object can be inactive in the map.
    location = dict(x=x/256, y=y/256, z_raw=p[114], pitch=p[115],
                    heading=p[116], bank=p[117])
    return dict(level=level, realspace_level=p[20], object_id=rep,
                location=location, location_source="Player.realspace_loc (save/load position)",
                map_object_location=object_location(res.get(base+8, b""), version, rep),
                detail=p[56], game_time=struct.unpack_from("<I", p, 28)[0])


def histogram(values):
    return dict(sorted(Counter(values).items()))


def span(values):
    return [min(values), max(values)] if values else None


def inspect_level(res, level, props, player, examples):
    base = 4000 + 100 * level
    info, raw = res[base+4], res[base+5]
    if len(info) != 58:
        raise ValueError(f"Unsupported FullMap size for level {level}: {len(info)}")
    width, height = struct.unpack_from("<II", info)
    if len(raw) != width * height * 16:
        raise ValueError(f"Bad MapElem array length for level {level}")
    version = int.from_bytes(res[base+2], "little")
    if version not in (11, 12):
        raise ValueError(f"Unsupported map version {version} in level {level}")
    result = dict(level=level, name=DECKS[level] if level < len(DECKS) else "Other",
                  version=version, cyber=bool(info[24]), width=width, height=height,
                  height_shift=struct.unpack_from("<I", info, 16)[0])
    if result["cyber"]:
        result["excluded"] = "Cyberspace; not a normal terrain inventory"
        return result
    ids_raw = res.get(base+7, b"")
    texture_ids = [v[0] for v in struct.iter_unpack("<H", ids_raw)]
    tiles = []
    for idx in range(width * height):
        t = raw[idx*16:(idx+1)*16]
        packed = int.from_bytes(t[6:8], "little")
        slots = dict(wall=packed & 63, floor=packed >> 11, ceiling=(packed >> 6) & 31)
        tiles.append(dict(x=idx % width, y=idx // width, type=t[0],
                          floor=t[1] & 31, ceiling=32-(t[2] & 31), param=t[3],
                          floor_rotation=(t[1] >> 5) & 3,
                          ceiling_rotation=(t[2] >> 5) & 3,
                          lock=t[8] & 31, flip=(t[8] >> 5) & 3,
                          family=bool(t[8] & 128), mirror=(t[9] >> 2) & 3,
                          neighbor_material=bool(t[9] & 1), zone=t[9] >> 5,
                          slots=slots))
    visible = [t for t in tiles if t["type"]]
    types = histogram(t["type"] for t in tiles)
    result["tile_types"] = {f"{k}:{NAMES.get(k, 'UNKNOWN')}": v for k, v in types.items()}
    result["non_solid_tiles"] = len(visible)
    result["heights"] = dict(floor=span([t["floor"] for t in visible]),
                             ceiling=span([t["ceiling"] for t in visible]),
                             base_clearance=span([t["ceiling"]-t["floor"] for t in visible]),
                             note="Map steps; base clearance excludes corner slope/mirror adjustments")
    for field in ("mirror", "flip", "lock", "floor_rotation", "ceiling_rotation", "param"):
        result[field] = histogram(t[field] for t in visible)
    result["family_bit_tiles"] = sum(t["family"] for t in visible)
    result["neighbor_material_tiles"] = sum(t["neighbor_material"] for t in visible)
    result["texture_ids"] = sorted(set(texture_ids))
    # Resource +42 stores four AnimTextureData records; not texture properties.
    anim_raw = res.get(base+42, b"")
    anim_size = 8 if version >= 12 else 7
    if len(anim_raw) % anim_size:
        raise ValueError(f"Bad animation group array for level {level}")
    groups = []
    for i in range(0, len(anim_raw), anim_size):
        a = anim_raw[i:i+anim_size]
        groups.append(dict(group=i//anim_size, speed=struct.unpack_from("<h", a)[0],
                           frame=a[4], frames=a[5], flags=a[6]))
    result["animation_groups"] = groups
    result["materials"] = dict(status="UNKNOWN: supply --textprop to classify sky and animated slots")
    sky, animated = [], []
    if props is not None:
        mapped = {}
        for slot, texture_id in enumerate(texture_ids):
            if texture_id >= len(props):
                continue
            p = props[texture_id]
            group = p["anim_group"]
            frames = groups[group]["frames"] if group < len(groups) else None
            mapped[slot] = dict(id=texture_id, **p, frames=frames)
        for t in visible:
            for face, slot in t["slots"].items():
                p = mapped.get(slot)
                if p and p["force_dir"] > 0:
                    sky.append(dict(x=t["x"], y=t["y"], face=face, slot=slot, **p))
                if p and p["frames"] is not None and p["frames"] > 1:
                    animated.append(dict(x=t["x"], y=t["y"], face=face, slot=slot, **p))
        result["materials"] = dict(status="Source-defined properties; selected faces may be hidden",
                                   sky_face_assignments=len(sky), animated_face_assignments=len(animated),
                                   sky_slots={k:v for k,v in mapped.items() if v["force_dir"]},
                                   animated_slots={k:v for k,v in mapped.items()
                                                   if v["frames"] is not None and v["frames"] > 1})
    origin = player.get("location") if player and player.get("level") == level else None
    def candidates(items):
        items = list(items)
        if origin:
            items.sort(key=lambda t: abs(t["x"]-origin["x"])+abs(t["y"]-origin["y"]))
        return items[:examples]
    def brief(t):
        return {k:t[k] for k in ("x", "y", "type", "floor", "ceiling", "param", "mirror")}
    boundaries = []
    for t in visible:
        for dx, dy in ((1,0), (0,1)):
            x, y = t["x"]+dx, t["y"]+dy
            if x >= width or y >= height:
                continue
            other = tiles[y*width+x]
            if other["type"] and (t["floor"], t["ceiling"]) != (other["floor"], other["ceiling"]):
                boundaries.append(dict(x=t["x"], y=t["y"], neighbor=[x,y],
                                       floor=[t["floor"],other["floor"]],
                                       ceiling=[t["ceiling"],other["ceiling"]]))
    # Long open row/column candidates: not traversal proofs (objects/doors ignored).
    runs = []
    for axis in (0,1):
        for line in range(height if axis == 0 else width):
            start = None
            length = width if axis == 0 else height
            for pos in range(length+1):
                x,y = (pos,line) if axis == 0 else (line,pos)
                open_tile = pos < length and tiles[y*width+x]["type"] == 1
                if open_tile and start is None:
                    start = pos
                if not open_tile and start is not None:
                    if pos-start >= 8:
                        sx,sy = (start,line) if axis == 0 else (line,start)
                        runs.append(dict(x=sx,y=sy,axis="x" if axis == 0 else "y",length=pos-start))
                    start = None
    result["route_hints"] = dict(
        status="Geometric candidates only; nearest Manhattan distance when current player is on level",
        slopes=candidates(brief(t) for t in visible if 6 <= t["type"] <= 21),
        diagonals=candidates(brief(t) for t in visible if 2 <= t["type"] <= 5),
        elevators=candidates(brief(t) for t in visible if t["zone"] == 7),
        height_boundaries=candidates(boundaries),
        low_ceilings=candidates(brief(t) for t in visible if 0 < t["ceiling"]-t["floor"] <= 4),
        long_open_runs=candidates(runs), sky=candidates(sky), animated=candidates(animated))
    return result


def inspect(path, props, examples):
    res = RESOURCES(path.read_bytes(), 4000, 5400)
    player = player_metadata(res)
    levels = [inspect_level(res, level, props, player, examples)
              for level in range(14) if 4005+100*level in res]
    if not levels:
        raise ValueError(f"No recognized level resources: {path}")
    return dict(file=str(path), evidence="DATA INVENTORY ONLY; no render/route acceptance", player=player, levels=levels)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archives", nargs="+")
    parser.add_argument("--textprop", type=Path)
    parser.add_argument("--examples", type=int, default=3)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    if args.examples < 0:
        parser.error("--examples must be nonnegative")
    paths = []
    for pattern in args.archives:
        matches = sorted(glob.glob(pattern))
        if not matches:
            parser.error(f"No file matches {pattern}")
        paths.extend(Path(p) for p in matches)
    props = texture_properties(args.textprop)
    reports = [inspect(p, props, args.examples) for p in dict.fromkeys(paths)]
    if args.json:
        print(json.dumps(reports, indent=2))
        return
    for report in reports:
        print(f"FILE {report['file']}\n{report['evidence']}")
        print("PLAYER", json.dumps(report["player"], sort_keys=True))
        for level in report["levels"]:
            print(f"LEVEL {level['level']} {level['name']} cyber={level['cyber']}")
            if level["cyber"]:
                continue
            for key in ("tile_types", "heights", "mirror", "flip", "lock", "floor_rotation",
                        "ceiling_rotation", "param", "family_bit_tiles", "neighbor_material_tiles",
                        "animation_groups", "materials", "route_hints"):
                print(f"  {key}: {json.dumps(level[key], sort_keys=True)}")
    print("Family bit is not proof of animation. Sky/animated materials require TEXTPROP.DAT.")


if __name__ == "__main__":
    main()
