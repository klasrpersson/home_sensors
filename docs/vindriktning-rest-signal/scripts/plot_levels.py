"""Plot CH1 of several captures above each other, with the ESP32-C3 input thresholds marked.

Needs Pillow. Each argument is capture.json=title.

    python3 plot_levels.py out.png ../captures/04-rest-esp-disconnected.json="ESP disconnected" ...
"""
import json
import sys

from PIL import Image, ImageDraw

out, items = sys.argv[1], [a.split("=", 1) for a in sys.argv[2:]]
W, ROW_H, M = 1600, 210, 70
img = Image.new("RGB", (W, ROW_H * len(items) + 10), "white")
draw = ImageDraw.Draw(img)

for row, (path, title) in enumerate(items):
    entry = json.load(open(path))["CHAN1"]
    pre = entry["preamble"]
    xinc = float(pre[4])
    yinc, yorig, yref = float(pre[7]), float(pre[8]), float(pre[9])
    volts = [(b - yorig - yref) * yinc for b in entry["data"]]
    # Point 688 of NORM data reads -2.16 V in every capture, a readout glitch; draw its neighbour instead
    volts = [v if v > -1 or i == 0 else volts[i - 1] for i, v in enumerate(volts)]
    top = 5 + row * ROW_H

    def y(v):
        return top + ROW_H - 25 - (v + 0.3) / 5.3 * (ROW_H - 45)

    def x(i):
        return M + i / (len(volts) - 1) * (W - M - 20)

    # Between VIL max and VIH min the ESP32-C3 may read either level
    draw.rectangle([M, y(2.475), W - 20, y(0.825)], fill=(255, 228, 228))
    for v, label in [(0, "0 V"), (0.825, "VIL 0.83"), (2.475, "VIH 2.48"), (3.3, "3.3 V"), (5, "5 V")]:
        draw.line([M, y(v), W - 20, y(v)], fill=(205, 205, 205))
        draw.text((4, y(v) - 6), label, fill="black")
    draw.line([(x(i), y(v)) for i, v in enumerate(volts)], fill=(0, 70, 170), width=2)
    span = len(volts) * xinc * 1e3
    draw.text((M, top), f"{title}   ({span:.0f} ms shown)", fill="black")

img.save(out)
print("saved", out)
