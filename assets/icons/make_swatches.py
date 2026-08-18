"""
Colour swatch generator.

The installed ODF packs are no help here: across 31 manifests and 2678 overlays they
declare exactly two colours, "random-saturated" and 0x000000. So the palette is curated
rather than harvested - four skin tones, three pale pinks and twelve hues, chosen to stay
apart from each other at arm's length in a headset, where two similar swatches are worse
than one fewer choice.

Each colour gets two swatches:
  swatch_<name>.svg      full strength     (RaceMenu alpha 1.0)
  swatch_<name>_70.svg   reduced strength  (RaceMenu alpha 0.7)

The reduced swatch is drawn at 70% opacity rather than as a lighter tint, because that is
literally what it does to the overlay - the swatch and the result match. 70 rather than
50: at half strength most of these hues stopped reading as the colour they name.

Plus swatch_default.svg, the "leave the pack's own colour alone" entry.

Keep PALETTE in step with kPalette in src/overlay/OverlayColors.cpp - the names are the
join between the two.

Usage:
    python make_swatches.py
    python convert_to_dds.py
"""

from pathlib import Path

_SCRIPT_DIR = Path(__file__).parent.resolve()
SVG_DIR = _SCRIPT_DIR / "svg"

# name, fill. Ordered as they appear in the row: skin tones (what most overlays want),
# then the pale pinks (blushes and flush overlays, too light to sit on the hue wheel
# without reading as a washed-out rose), then neutrals, then round the wheel. The row runs every colour at full strength first
# and only then repeats them at reduced strength, so reaching a solid colour never means
# scrolling past a faded one.
PALETTE = [
    ("skin_pale",   "#f7e0cb"),
    ("skin_pink",   "#edb8a6"),
    ("skin_normal", "#c68642"),
    ("skin_dark",   "#6f4327"),
    ("blush",   "#f9cbdb"),
    ("petal",   "#f2a9c4"),
    ("orchid",  "#e0a9d5"),
    ("white",   "#f2f2f2"),
    ("ash",     "#8c8c8c"),
    ("black",   "#1a1a1a"),
    ("crimson", "#c0392b"),
    ("amber",   "#e67e22"),
    ("gold",    "#f1c40f"),
    ("moss",    "#4f8f3a"),
    ("teal",    "#16a085"),
    ("azure",   "#2980b9"),
    ("indigo",  "#5b4b8a"),
    ("violet",  "#8e44ad"),
    ("rose",    "#d6336c"),
]

# Must match the alpha on the _70 rows of kPalette in src/overlay/OverlayColors.cpp.
REDUCED_ALPHA = 0.7

# The icons in this library are 512px on a 256-unit viewBox.
HEADER = ('<svg xmlns="http://www.w3.org/2000/svg" width="512px" height="512px" '
          'viewBox="0 0 256 256">')

# A rim in the library's dark tone, so a white swatch still reads as a disc against a
# bright wall and a black one against a night sky.
RIM = '<circle cx="128" cy="128" r="104" fill="none" stroke="#262626" stroke-width="10"/>'


def swatch(fill: str, opacity: float) -> str:
    return (f'{HEADER}'
            f'<circle cx="128" cy="128" r="104" fill="{fill}" fill-opacity="{opacity:g}"/>'
            f'{RIM}'
            f'</svg>\n')


def default_swatch() -> str:
    """The no-override entry: an empty ring with a slash through it."""
    return (f'{HEADER}'
            f'<circle cx="128" cy="128" r="104" fill="none" stroke="#8c8c8c" stroke-width="14"/>'
            f'<line x1="60" y1="196" x2="196" y2="60" stroke="#8c8c8c" stroke-width="14" '
            f'stroke-linecap="round"/>'
            f'</svg>\n')


def main():
    SVG_DIR.mkdir(parents=True, exist_ok=True)
    written = 0

    for name, fill in PALETTE:
        (SVG_DIR / f"swatch_{name}.svg").write_text(swatch(fill, 1.0), encoding="utf-8")
        (SVG_DIR / f"swatch_{name}_70.svg").write_text(swatch(fill, REDUCED_ALPHA), encoding="utf-8")
        print(f"  [OK] swatch_{name}.svg + _70")
        written += 2

    (SVG_DIR / "swatch_default.svg").write_text(default_swatch(), encoding="utf-8")
    print("  [OK] swatch_default.svg")
    written += 1

    print(f"\nWrote {written} SVGs. Run convert_to_dds.py next.")


if __name__ == "__main__":
    main()
