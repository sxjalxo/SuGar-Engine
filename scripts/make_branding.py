"""Derives every branding file from one master image.

The master is a square, transparent-background PNG holding the cube above the wordmark.
Everything the engine ships is derived from it, so re-running this after an art change is
the whole update:

    python scripts/make_branding.py [path/to/master.png]

Outputs (all under assets/branding/):
    sugar_logo.png  full lockup, trimmed - the editor panel logo
    sugar_cube.png  cube only, squared, 512x512 - the window icon
    sugar_icon.ico  cube only, 16..256 - the executable's shell icon

The cube is separated from the wordmark by finding the transparent gap between them, so
the crop follows the art instead of hard-coded pixel rows.
"""

import os
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BRANDING = os.path.join(ROOT, "assets", "branding")
DEFAULT_MASTER = os.path.join(BRANDING, "sugar_master.png")
ICON_SIZES = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
ALPHA_FLOOR = 8  # below this an "opaque" pixel is just anti-aliasing noise


def content_box(alpha, top, bottom):
    band = alpha[top:bottom]
    rows = np.where(band.max(axis=1) > ALPHA_FLOOR)[0]
    cols = np.where(band.max(axis=0) > ALPHA_FLOOR)[0]
    if rows.size == 0 or cols.size == 0:
        raise SystemExit("branding master has no opaque pixels in the requested band")
    return int(cols.min()), top + int(rows.min()), int(cols.max()) + 1, top + int(rows.max()) + 1


def first_gap(alpha, min_height=10):
    """The first transparent band tall enough to be the gap between cube and wordmark."""
    filled = (alpha > ALPHA_FLOOR).sum(axis=1)
    start = None
    for y, count in enumerate(filled):
        if count == 0:
            if start is None:
                start = y
        elif start is not None:
            if y - start >= min_height and start > 0:
                return start
            start = None
    return alpha.shape[0]


def main():
    master_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_MASTER
    master = Image.open(master_path).convert("RGBA")
    alpha = np.asarray(master)[:, :, 3]
    os.makedirs(BRANDING, exist_ok=True)

    # --- full lockup, trimmed with a small transparent margin -----------------------
    pad = 24
    left, top, right, bottom = content_box(alpha, 0, master.height)
    logo = master.crop((max(0, left - pad), max(0, top - pad),
                        min(master.width, right + pad), min(master.height, bottom + pad)))
    logo.save(os.path.join(BRANDING, "sugar_logo.png"))

    # --- cube only, squared so the icon is not letterboxed --------------------------
    cube = master.crop(content_box(alpha, 0, first_gap(alpha)))
    side = max(cube.size) + 16
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(cube, ((side - cube.width) // 2, (side - cube.height) // 2), cube)
    square = square.resize((512, 512), Image.LANCZOS)
    square.save(os.path.join(BRANDING, "sugar_cube.png"))
    square.save(os.path.join(BRANDING, "sugar_icon.ico"), sizes=ICON_SIZES)

    print("logo {}, cube {}, icon {} sizes".format(logo.size, square.size, len(ICON_SIZES)))


if __name__ == "__main__":
    main()
