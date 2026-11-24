"""Build an IAQ8 chassis mockup texture from LABQ8's own background.png.

Covers LABQ8-specific elements (logo, top controls, knob panel, descriptor
text) with texture quilted from clean areas of the same image, then composes
an "IAQ8" logo from the real LABQ8 logo glyphs.
"""
import random
import sys

import numpy as np
from PIL import Image, ImageFilter

src_path, out_path = sys.argv[1], sys.argv[2]
random.seed(8)
base = Image.open(src_path).convert("RGB")
W, H = base.size
arr = np.asarray(base).astype(np.float32)


def feather_mask(w, h, f):
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    d = np.minimum.reduce([xx, yy, w - 1 - xx, h - 1 - yy])
    return np.clip(d / f, 0, 1)


def quilt(patch_box, w, h, tiles=None):
    """Large texture from one clean patch: random flips, offsets, feathered."""
    patch = np.asarray(base.crop(patch_box)).astype(np.float32)
    ph, pw = patch.shape[:2]
    # Sequential "last tile wins" compositing on a jittered grid: keeps the
    # grain and wear of the source instead of averaging it away.
    tex = np.ones((h, w, 3), np.float32) * patch.reshape(-1, 3).mean(0)
    sx, sy = int(pw * .62), int(ph * .62)
    spots = [(gx, gy) for gy in range(-ph // 3, h, sy) for gx in range(-pw // 3, w, sx)]
    random.shuffle(spots)
    m = feather_mask(pw, ph, min(pw, ph) * .18)[..., None]
    for gx, gy in spots:
        p = patch
        if random.random() < .5: p = p[:, ::-1]
        if random.random() < .5: p = p[::-1, :]
        x0 = gx + random.randint(-sx // 4, sx // 4)
        y0 = gy + random.randint(-sy // 4, sy // 4)
        xs, ys = max(0, x0), max(0, y0)
        xe, ye = min(w, x0 + pw), min(h, y0 + ph)
        if xe <= xs or ye <= ys: continue
        sub = p[ys - y0:ye - y0, xs - x0:xe - x0]
        ms = m[ys - y0:ye - y0, xs - x0:xe - x0]
        tex[ys:ye, xs:xe] = tex[ys:ye, xs:xe] * (1 - ms) + sub * ms
    return tex


def cover(box, patch_box, feather=12, ring=10, sides_only=False):
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    tex = quilt(patch_box, w, h)
    # match mean colour to the ring around the region (keeps local shading)
    ring_px = np.concatenate([
        arr[max(0, y0 - ring):y0, x0:x1].reshape(-1, 3),
        arr[y1:y1 + ring, x0:x1].reshape(-1, 3),
        arr[y0:y1, max(0, x0 - ring):x0].reshape(-1, 3),
        arr[y0:y1, x1:x1 + ring].reshape(-1, 3)])
    if sides_only:
        ring_px = np.concatenate([arr[y0:y1, max(0, x0 - 40):x0].reshape(-1, 3), arr[y0:y1, x1:x1 + 40].reshape(-1, 3)])
    tex += ring_px.mean(0) - tex.reshape(-1, 3).mean(0)
    m = feather_mask(w, h, feather)[..., None]
    arr[y0:y1, x0:x1] = arr[y0:y1, x0:x1] * (1 - m) + tex * m


GREEN_PATCH = (108, 312, 312, 588)
DARK_PATCH = (122, 606, 322, 664)

# 1. LABQ8 logo and descriptor text (keep "VitaPlug ——")
logo_src = base.copy()
cover((112, 100, 392, 161), GREEN_PATCH, feather=8, ring=6)
cover((112, 196, 312, 308), GREEN_PATCH)
# 2. top controls: STEREO MODE, POLARITY, INTENTION
cover((424, 38, 1266, 166), GREEN_PATCH, feather=10, sides_only=True)
# 3. knob panel interior
cover((100, 604, 1572, 884), DARK_PATCH, feather=6, ring=3)

out = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))

# 4. "IAQ8" from the real glyphs: I = stem of L, then A, Q, 8
la = np.asarray(logo_src).astype(np.float32)
def glyph(box):
    g = logo_src.crop(box)
    r = np.asarray(g).astype(np.float32)[..., 0]
    a = np.clip((r - 95) / 70, 0, 1)
    a = (a * 255).astype(np.uint8)
    mask = Image.fromarray(a).filter(ImageFilter.GaussianBlur(.4))
    return g, mask

y0, y1 = 108, 160
pieces = [((123, y0, 136, y1), 0), ((160, y0, 220, y1), 9), ((276, y0, 330, y1), 6), ((334, y0, 382, y1), 7)]
x = 123
for box, gap in pieces:
    x += gap
    g, m = glyph(box)
    out.paste(g, (x, y0), m)
    x += box[2] - box[0]

out.save(out_path)
print("logo ends at x =", x)

# 5. restore the lower panel screws from the original
from PIL import ImageDraw
for cx, cy in [(105, 613), (1566, 613), (105, 873), (1566, 873)]:
    r = 13
    box = (cx - r, cy - r, cx + r, cy + r)
    m = Image.new("L", (2 * r, 2 * r), 0)
    ImageDraw.Draw(m).ellipse((1, 1, 2 * r - 2, 2 * r - 2), fill=255)
    out.paste(logo_src.crop(box), box[:2], m.filter(ImageFilter.GaussianBlur(1.2)))
out.save(out_path)
