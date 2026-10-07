"""Draws assets/simpsons.ico: the launcher's pink-frosted donut (same design
as DonutIcon() in launcher/main.cpp), at the sizes Windows asks for."""
import math
import os
from PIL import Image

def donut(n):
    im = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    px = im.load()
    sprinkles = [(219, 156, 45), (96, 174, 39), (76, 201, 242), (255, 255, 255), (87, 87, 235)]
    for y in range(n):
        for x in range(n):
            fx, fy = (x + 0.5) / n - 0.5, (y + 0.5) / n - 0.5
            r = math.hypot(fx, fy)
            if r > 0.47 or r < 0.15:
                continue
            c = [214, 160, 94]
            wob = 0.012 * math.sin(math.atan2(fy, fx) * 9)
            frost = 0.18 + wob < r < 0.41 + wob
            if frost:
                c = [247, 140, 186]
            s = 1 - 0.35 * max(0.0, fx + fy)
            c = [min(255, int(v * s)) for v in c]
            if frost and n >= 32:
                g = max(1, n // 16)
                cell = ((x // g) * 7 + (y // g) * 13) % 11
                if cell < 5 and (x % g) < max(1, n // 48) and (y % g) < max(1, n // 24):
                    c = list(sprinkles[cell])
            px[x, y] = (c[0], c[1], c[2], 255)
    return im

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
big = donut(256)
big.save(os.path.join(root, "assets", "simpsons.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
big.save(os.path.join(root, "assets", "simpsons.png"))
print("wrote assets/simpsons.ico")
