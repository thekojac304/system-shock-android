"""Make ignored material contact sheets from the user's owned texture resource."""
from collections import Counter
import json
from pathlib import Path
import runpy
import sys

from PIL import Image, ImageDraw

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'dist/gpu-grove1-decklight-diag1'
data = json.loads((OUT / 'decklight-diagnostics.json').read_text())
usage = Counter()
for deck in data['decks'].values():
    for key, faces in deck['mapped_materials'].items():
        usage[int(key)] += sum(faces.values())
baseline = {20, 35, 36, 82, 101, 144, 185, 215, 216, 217}
ids = sorted((i for i in usage if i and i not in baseline), key=lambda i: (-usage[i], i))
resources = runpy.run_path(str(ROOT / 'scripts/inspect-vis1-map.py'))['resources']
texture = resources((ROOT / 'dist/horror-author1/owned-TEXTURE.RES').read_bytes(), 1000, 1400)
palette = (ROOT / 'dist/vis1/palette.bin').read_bytes()
for page, first in enumerate(range(0, len(ids), 100)):
    sheet = Image.new('RGB', (1000, 1200), (22, 22, 22))
    draw = ImageDraw.Draw(sheet)
    for slot, tid in enumerate(ids[first:first + 100]):
        raw = texture.get(1000 + tid)
        if raw:
            offset = int.from_bytes(raw[2:6], 'little')
            frame = raw[offset:]
            w = int.from_bytes(frame[8:10], 'little')
            h = int.from_bytes(frame[10:12], 'little')
            if 0 < w <= 512 and 0 < h <= 512 and len(frame) >= 28 + w * h:
                image = Image.frombytes('P', (w, h), bytes(frame[28:28 + w * h]))
                image.putpalette(palette)
                sheet.paste(image.convert('RGB').resize((92, 92)), ((slot % 10) * 100 + 4, (slot // 10) * 120 + 4))
        draw.text(((slot % 10) * 100 + 4, (slot // 10) * 120 + 100),
                  f'{tid}  n={usage[tid]}', fill='white')
    path = OUT / f'unreviewed-materials-{page + 1}.png'
    sheet.save(path)
    print(path)
