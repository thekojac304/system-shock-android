"""Read owned ARCHIVE.DAT locally or from the connected QA app; print metadata only."""
import pathlib
import subprocess
import sys


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


def resources(data, first=4100, last=4200):
    u = lambda b: int.from_bytes(b, 'little')
    directory = u(data[124:128])
    count, offset = u(data[directory:directory+2]), u(data[directory+2:directory+6])
    result = {}
    for i in range(count):
        entry = data[directory+6+i*10:directory+16+i*10]
        rid, size, flags, packed = u(entry[:2]), u(entry[2:5]), entry[5], u(entry[6:9])
        chunk = data[offset:offset+packed]
        offset = (offset + packed + 3) & ~3
        if first <= rid < last:
            if flags & 1:
                if flags & 2:
                    header=2+4*(u(chunk[:2])+1)
                    chunk=chunk[:header]+expand(chunk[header:])
                else:
                    chunk = expand(chunk)
            assert len(chunk) == size
            result[rid] = chunk
    return result


if __name__ == '__main__':
    if sys.argv[1] == '--adb':
        data = subprocess.run([sys.argv[2], 'exec-out', 'run-as',
            'io.github.raposomiguel50.systemshock.qa', 'cat', 'files/res/data/ARCHIVE.DAT'],
            check=True, capture_output=True).stdout
    else:
        data = pathlib.Path(sys.argv[1]).read_bytes()
    r = resources(data)
    print('Medical resources:', {k: len(v) for k, v in r.items()})
    tiles = r[4105]
    print('Tile types (hex), x=24..38, y=16..29; source player start=(30,22):')
    for y in range(16, 30):
        print(y, ''.join(format(tiles[(y*64+x)*16], 'x') for x in range(24, 39)))
    print('Texture slot -> texture ID:', list(enumerate(int.from_bytes(r[4107][i:i+2], 'little') for i in range(0,len(r[4107]),2))))
    for y in range(20, 25):
        for x in range(26, 31):
            t = tiles[(y*64+x)*16:(y*64+x+1)*16]
            if not t[0]:
                continue
            tm = int.from_bytes(t[6:8], 'little')
            print(f'tile=({x},{y}) type={t[0]} h={t[1]&31}/{t[2]&31} '
                  f'texture slots wall/floor/ceil={tm&63}/{tm>>11}/{(tm>>6)&31} '
                  f'flags={t[8:12].hex()} objRef={int.from_bytes(t[4:6], "little")}')
    for rid, chunk in r.items():
        if rid == 4108:
            for i in range(0, len(chunk), 27):
                o = chunk[i:i+27]
                if len(o) != 27 or not o[0]:
                    continue
                x, y = int.from_bytes(o[11:13], 'little')/256, int.from_bytes(o[13:15], 'little')/256
                if 26 <= x < 35 and 18 <= y < 28:
                    print(f'object={i//27} class/sub/type={o[1]}/{o[2]}/{o[20]} pos=({x:.2f},{y:.2f}) z={o[15]}')
