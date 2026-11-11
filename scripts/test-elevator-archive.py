"""Check elevator destinations and object-reference chains in owned game data.

Usage: python scripts/test-elevator-archive.py ARCHIVE.DAT [CurrentGame.dat ...]
This inspects data only; it does not simulate gameplay, music or level loading.
"""
import pathlib
import struct
import sys


def expand(data):
    table = [bytes([i]) for i in range(256)]
    output = bytearray()
    bit, old = 0, None
    while bit + 14 <= len(data) * 8:
        pos, shift = divmod(bit, 8)
        code = (int.from_bytes(data[pos:pos + 3].ljust(3, b'\0'), 'big') >> (10 - shift)) & 16383
        bit += 14
        if code == 16383:
            break
        if code == 16382:
            table, old = [bytes([i]) for i in range(256)], None
            continue
        value = table[code] if code < len(table) else old + old[:1]
        output += value
        if old is not None and len(table) < 16382:
            table.append(old + value[:1])
        old = value
    return output


def read_archive(path):
    data = path.read_bytes()
    directory = int.from_bytes(data[124:128], 'little')
    count, offset = struct.unpack_from('<HI', data, directory)
    resources = {}
    for i in range(count):
        entry = data[directory + 6 + i * 10:directory + 16 + i * 10]
        rid = int.from_bytes(entry[:2], 'little')
        size = int.from_bytes(entry[2:5], 'little')
        compressed = int.from_bytes(entry[6:9], 'little')
        chunk = data[offset:offset + compressed]
        offset = (offset + compressed + 3) & ~3
        if 4000 <= rid < 5400 and rid % 100 in (2, 3, 4, 5, 8, 9, 19):
            assert not entry[5] & 2, f'compound level resource {rid}'
            if entry[5] & 1:
                chunk = expand(chunk)
            assert len(chunk) == size, f'bad resource size {rid}'
            resources[rid] = chunk
    return resources


def check(path):
    resources = read_archive(path)
    decks = {}
    for level in range(14):
        base = 4000 + level * 100
        if base + 5 not in resources:
            continue
        info, tiles = resources[base + 4], resources[base + 5]
        width, height = struct.unpack_from('<II', info)
        assert (width, height) == (64, 64)
        version = int.from_bytes(resources[base + 2], 'little')
        record_size = 28 if version >= 12 else 27
        raw_objects = resources[base + 8]
        assert len(raw_objects) % record_size == 0
        objects = []
        for offset in range(0, len(raw_objects), record_size):
            record = raw_objects[offset:offset + record_size]
            pad = int(version >= 12)
            objects.append(dict(active=record[0], obclass=record[1], subclass=record[2],
                                spec=int.from_bytes(record[3 + pad:5 + pad], 'little'),
                                x=int.from_bytes(record[11 + pad:13 + pad], 'little'),
                                y=int.from_bytes(record[13 + pad:15 + pad], 'little'),
                                type=record[20 + pad]))
        refs = list(struct.iter_unpack('<HHHHH', resources[base + 9]))
        fixtures = resources[base + 19]
        decks[level] = dict(tiles=tiles, objects=objects, refs=refs, fixtures=fixtures)

    panel_count = link_count = 0
    for level, deck in decks.items():
        tiles, objects, refs = deck['tiles'], deck['objects'], deck['refs']
        for y in range(64):
            for x in range(64):
                tile = tiles[(y * 64 + x) * 16:(y * 64 + x + 1) * 16]
                if tile[9] >> 5 != 7:
                    continue
                # Legacy elevator rectangle walks assume the zone cannot reach
                # the map edge. Check that assumption without dereferencing it.
                assert 0 < x < 63 and 0 < y < 63, f'edge elevator zone {level}:{x},{y}'
                ref = int.from_bytes(tile[4:6], 'little')
                seen = set()
                while ref:
                    assert 0 < ref < len(refs), f'invalid reference {level}:{ref}'
                    assert ref not in seen, f'cyclic tile references {level}:{x},{y}'
                    seen.add(ref)
                    rx, ry, obj, ref, _ = refs[ref]
                    assert (rx, ry) == (x, y), f'reference in wrong tile {level}:{obj}'
                    assert 0 < obj < len(objects), f'invalid object {level}:{obj}'
                    assert objects[obj]['active'], f'inactive referenced object {level}:{obj}'

        for obj_id, obj in enumerate(objects):
            if not (obj['active'] and obj['obclass'] == 9 and obj['subclass'] == 3 and 4 <= obj['type'] <= 6):
                continue
            panel_count += 1
            fixture = deck['fixtures'][obj['spec'] * 30:(obj['spec'] + 1) * 30]
            assert len(fixture) == 30 and int.from_bytes(fixture[:2], 'little') == obj_id
            p1, p2, p3, p4 = struct.unpack_from('<IIII', fixture, 12)
            destination_panels = [p1 >> 16, p1 & 65535, p2 >> 16, p2 & 65535, p3 >> 16, p3 & 65535]
            destinations = [dest for dest in range(16) if p4 & (1 << dest)]
            assert len(destinations) <= 6, f'too many panel links {level}:{obj_id}'
            for dest, dest_panel in zip(destinations, destination_panels):
                assert dest in decks, f'missing destination {dest}'
                dest_objs = decks[dest]['objects']
                assert 0 < dest_panel < len(dest_objs), f'invalid destination panel {level}:{obj_id}->{dest}:{dest_panel}'
                target = dest_objs[dest_panel]
                assert target['active'] and target['obclass'] == 9 and target['subclass'] == 3 and 4 <= target['type'] <= 6
                tx, ty = target['x'] >> 8, target['y'] >> 8
                assert 0 <= tx < 64 and 0 <= ty < 64
                assert decks[dest]['tiles'][(ty * 64 + tx) * 16 + 9] >> 5 == 7
                link_count += 1
                if level == 3:
                    print(f'MAINTENANCE panel={obj_id} tile={obj["x"] >> 8},{obj["y"] >> 8} '
                          f'destination={dest} panel={dest_panel} tile={tx},{ty}')
    assert panel_count and link_count, 'no elevator data tested'
    print(f'ELEVATOR_ARCHIVE=PASS file={path.name} panels={panel_count} links={link_count} '
          '(data checks only; hardware transition QA required)')


if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    for filename in sys.argv[1:]:
        check(pathlib.Path(filename))
