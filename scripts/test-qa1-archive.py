"""Validate QA spawn selection with owned game data; never copies it into the repo.
Usage: python scripts/test-qa1-archive.py ARCHIVE.DAT touch_qa_test.exe
"""
import pathlib
import subprocess
import sys
import tempfile


def expand(data):
    table = [bytes([i]) for i in range(256)]
    output = bytearray()
    bit, old = 0, None
    while bit + 14 <= len(data) * 8:
        pos, shift = divmod(bit, 8)
        code = (int.from_bytes(data[pos:pos+3].ljust(3, b'\0'), 'big') >> (10-shift)) & 16383
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


data = pathlib.Path(sys.argv[1]).read_bytes()
u = lambda b: int.from_bytes(b, 'little')
directory = u(data[124:128])
count, offset = u(data[directory:directory+2]), u(data[directory+2:directory+6])
resources = {}
for i in range(count):
    entry = data[directory+6+i*10:directory+16+i*10]
    rid, size, flags, compressed = u(entry[:2]), u(entry[2:5]), entry[5], u(entry[6:9])
    chunk = data[offset:offset+compressed]
    offset = (offset + compressed + 3) & ~3
    if 4000 <= rid < 5400 and rid % 100 in (4, 5):
        if flags & 1:
            chunk = expand(chunk)
        assert len(chunk) == size
        resources[rid] = chunk

with tempfile.TemporaryDirectory(prefix='qa1-') as temp:
    tiles = pathlib.Path(temp) / 'tiles.bin'
    for level in [1, 2, 0, 3, 4, 5, 6, 11, 12, 13, 7, 8, 9]:
        info = resources[4004+level*100]
        assert u(info[:4]) == u(info[4:8]) == 64 and not info[24]
        tiles.write_bytes(resources[4005+level*100])
        result = subprocess.run([sys.argv[2], str(tiles), str(u(info[16:20])), str(int(level==13))],
                                check=True, capture_output=True, text=True)
        print(f'LEVEL={level} {result.stdout.strip()}')
print('QA1_ARCHIVE_GEOMETRY=PASS (13 destinations; actual gameplay still needs hardware QA)')
