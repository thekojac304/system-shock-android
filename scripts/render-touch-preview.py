"""Render the actual SDL commands recorded by touch_render_test's optional path prefix.
Usage: python scripts/render-touch-preview.py dist/touch-tests/touch-preview-*.jsonl
These are UI previews with a placeholder background, not Android screenshots.
"""
import glob
import json
import sys
from pathlib import Path
from PIL import Image, ImageDraw

for pattern in sys.argv[1:]:
    for filename in glob.glob(pattern):
        image = Image.new("RGB", (1920, 960), (6, 10, 14))
        draw = ImageDraw.Draw(image, "RGBA")
        draw.rectangle((320, 0, 1600, 960), fill=(30, 43, 52, 255))
        for line in Path(filename).read_text().splitlines():
            kind, red, green, blue, alpha, x, y, a, b = json.loads(line)
            color = (red, green, blue, alpha)
            if kind == "line":
                draw.line((x, y, a, b), fill=color)
            elif kind == "fill":
                draw.rectangle((x, y, x + a, y + b), fill=color)
            else:
                draw.rectangle((x, y, x + a, y + b), outline=color)
        output = Path(filename).with_suffix(".png")
        image.save(output)
        print(output)
