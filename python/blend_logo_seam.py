"""Soften the hard vertical seam in the split tree / code-rain logo.

The draft logo is a composite: a teal lidar point-cloud tree on the left and
green code rain on the right, joined along a single vertical line at the
trunk. Neither source half extends past that line, so the blend is built
from the composite itself:

  1. a hue ramp that turns the tree's teal into the rain's green gradually
     across a band around the seam, instead of switching at one column;
  2. a faint mirrored "ghost" of the tree's foliage carried into the rain
     side, thinning out into scattered points with distance from the trunk;
  3. a faint copy of the nearest rain columns spilling onto the tree side,
     fading out with distance from the trunk.

Layers are combined with a screen blend, which on a near-black background
behaves like adding light and never darkens anything already present.

Usage:
  python blend_logo_seam.py <input.png> <output.png> [--seam 600]
"""

import argparse

import numpy as np
from PIL import Image


#1. helper functions -------------------------------------------------------

def rgb_to_hsv(rgb):
    """Convert an (..., 3) float array in 0-1 to hue (degrees), sat, value."""
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    c_max = rgb.max(-1)
    c_min = rgb.min(-1)
    delta = c_max - c_min
    safe_delta = np.where(delta == 0, 1, delta)

    hue = np.zeros_like(c_max)
    hue = np.where(c_max == r, ((g - b) / safe_delta) % 6, hue)
    hue = np.where(c_max == g, (b - r) / safe_delta + 2, hue)
    hue = np.where(c_max == b, (r - g) / safe_delta + 4, hue)
    hue = np.where(delta == 0, 0, hue) * 60

    sat = np.where(c_max == 0, 0, delta / np.where(c_max == 0, 1, c_max))
    return hue, sat, c_max


def hsv_to_rgb(hue, sat, val):
    """Inverse of rgb_to_hsv; returns an (..., 3) float array in 0-1."""
    h6 = (hue % 360) / 60
    chroma = val * sat
    x = chroma * (1 - np.abs(h6 % 2 - 1))
    m = val - chroma
    zeros = np.zeros_like(hue)

    sector = np.floor(h6).astype(int) % 6
    r = np.choose(sector, [chroma, x, zeros, zeros, x, chroma])
    g = np.choose(sector, [x, chroma, chroma, x, zeros, zeros])
    b = np.choose(sector, [zeros, zeros, x, chroma, chroma, x])
    return np.stack([r + m, g + m, b + m], -1)


def shift_hue(layer, shift_deg):
    """Rotate each pixel's hue by a per-column amount (degrees)."""
    hue, sat, val = rgb_to_hsv(layer)
    return hsv_to_rgb(hue + shift_deg[None, :], sat, val)


def screen(a, b):
    """Screen blend: adds light without ever darkening."""
    return 1 - (1 - a) * (1 - b)


#2. main --------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--seam", type=int, default=600,
                        help="x position of the vertical seam, in pixels")
    parser.add_argument("--art-bottom", type=int, default=900,
                        help="rows below this (the text line) are left alone")
    parser.add_argument("--hue-left", type=float, default=166,
                        help="typical hue of the tree, degrees")
    parser.add_argument("--hue-right", type=float, default=135,
                        help="typical hue of the rain, degrees")
    parser.add_argument("--hue-band", type=float, default=110,
                        help="width scale of the teal-to-green ramp, pixels")
    parser.add_argument("--ghost-reach", type=float, default=90,
                        help="distance over which the tree ghost fades, pixels")
    parser.add_argument("--rain-reach", type=float, default=55,
                        help="distance over which the rain spill fades, pixels")
    parser.add_argument("--glyph-margin", type=int, default=14,
                        help="width of the glyph column just left of the "
                             "seam, kept out of the ghost and spill, pixels")
    parser.add_argument("--seed", type=int, default=2)
    args = parser.parse_args()

    #3. load image and separate the art from the flat background
    #  - the background is a flat near-black; subtracting it leaves just the
    #    light contributed by the tree and the rain
    img = np.asarray(Image.open(args.input).convert("RGB")).astype(float) / 255
    height, width, _ = img.shape
    background = np.median(img[:40, :40].reshape(-1, 3), axis=0)
    art = np.clip(img[:args.art_bottom] - background, 0, 1)

    seam = args.seam
    xs = np.arange(width)
    tree = np.where(xs[None, :, None] < seam, art, 0)
    rain = np.where(xs[None, :, None] >= seam, art, 0)

    #4. build the per-column hue ramp
    #  - ramp runs smoothly from 0 (far left) to 1 (far right), 0.5 at seam
    #  - each side is rotated toward the ramp's hue at that column
    ramp = 1 / (1 + np.exp(-(xs - seam) / (args.hue_band / 4)))
    target_hue = args.hue_left * (1 - ramp) + args.hue_right * ramp
    tree_shift = target_hue - args.hue_left
    rain_shift = target_hue - args.hue_right

    tree = shift_hue(tree, tree_shift)
    rain = shift_hue(rain, rain_shift)

    #5. tree ghost: mirror the tree onto the rain side
    #  - the mirror line is the tree's own right edge (just left of the
    #    composite's glyph column), so the ghost starts right where the tree
    #    stops and runs straight through the dark gap at the seam
    #  - only the tree is mirrored: the glyph column just left of the seam
    #    would otherwise be flipped across and stack mirrored characters
    #    beside the real ones
    #  - brightness falls off with distance from the mirror line
    #  - pixels are randomly dropped more often further out, so the ghost
    #    breaks up into scattered points rather than fading as a smooth wash
    rng = np.random.default_rng(args.seed)
    axis = seam - args.glyph_margin
    tree_only = np.where(xs[None, :, None] < axis, art, 0)
    mirrored = tree_only[:, ::-1]
    offset = 2 * axis - width
    ghost = np.zeros_like(art)
    if offset >= 0:
        ghost[:, offset:] = mirrored[:, :width - offset]
    else:
        ghost[:, :width + offset] = mirrored[:, -offset:]
    distance_right = np.clip(xs - axis, 0, None).astype(float)
    ghost_weight = 0.6 * np.exp(-distance_right / args.ghost_reach)
    keep_prob = np.exp(-distance_right / (args.ghost_reach * 1.3))
    keep = rng.random(ghost.shape[:2]) < keep_prob[None, :]
    ghost = ghost * (ghost_weight * (xs >= axis))[None, :, None] * keep[..., None]
    ghost = shift_hue(ghost, target_hue - args.hue_left)

    #6. rain spill: copy the first rain columns leftward onto the tree side
    #  - a straight shift, not a mirror, so the characters stay readable
    #  - brightness falls off with distance left of the seam
    spill_width = int(args.rain_reach * 3)
    spill = np.zeros_like(art)
    spill[:, seam - spill_width:seam] = art[:, seam:seam + spill_width]
    distance_left = np.clip(seam - xs, 0, None).astype(float)
    #  - the weight starts at zero for the first ~12 px, where the composite
    #    already has its own glyph column, and eases in after that
    ease_in = np.clip((distance_left - args.glyph_margin) / 14, 0, 1)
    spill_weight = 0.55 * np.exp(-distance_left / args.rain_reach) * ease_in
    spill = spill * (spill_weight * (xs < seam))[None, :, None]
    spill = shift_hue(spill, target_hue - args.hue_right)

    #7. keep the spill off pixels that are already lit
    #  - without this, copied glyphs stack on top of the glyph column and
    #    trunk that already sit just left of the seam, producing garbled
    #    double characters
    tree_brightness = tree.max(-1, keepdims=True)
    occupancy = np.clip(tree_brightness / 0.25, 0, 1)
    spill = spill * (1 - occupancy)

    #8. combine the layers
    blended = screen(screen(tree, rain), screen(ghost, spill))

    #9. put the background back
    out = img.copy()
    out[:args.art_bottom] = np.clip(blended + background, 0, 1)

    Image.fromarray((out * 255).round().astype(np.uint8)).save(args.output)
    print(f"wrote {args.output} ({width}x{height}), seam at x={seam}")


if __name__ == "__main__":
    main()
