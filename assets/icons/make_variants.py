"""
Icon variant generator.

The icon library in this workspace has one convention: a base icon is drawn with
gradients running #ffffff -> #262626 (80% opaque), and its selected state is the same
artwork with those two stops swapped for #2e2b54 -> #da619b, fully opaque. Everything
here is built from that one rule.

Produces:
  <name>_highlight.svg      the whole icon in the highlight palette
  <name>_<pct>.svg          a gauge: the bottom <pct>% in the highlight palette, the
                            rest in the base palette, so the icon reads as filling up

Usage:
    python make_variants.py

Idempotent - rerunning overwrites the generated SVGs and touches nothing else.
Hand-authored bases live in svg/ alongside the output; only the names listed in
BASES and GAUGES are ever written.
"""

import re
from pathlib import Path

_SCRIPT_DIR = Path(__file__).parent.resolve()
SVG_DIR = _SCRIPT_DIR / "svg"

# Base palette -> highlight palette. Matches skse/VR-Editor/assets/icons/recolor_highlights.py.
BASE_LIGHT = "#ffffff"
BASE_DARK = "#262626"
HL_LIGHT = "#2e2b54"
HL_DARK = "#da619b"

# Icons that get a plain _highlight variant.
BASES = ["dice", "weight", "paint-palette", "tng-addon"]

# Icons that get fill-level variants, and the levels they get.
GAUGES = {"weight": [0, 25, 50, 75, 100]}

# Icons that are another icon turned on the spot: name -> (source, degrees clockwise).
# The chevrons are one piece of artwork pointed four ways rather than four drawings that
# have to be kept looking like each other.
ROTATIONS = {
    "chevron-left": ("chevron-down", 90),
    "chevron-right": ("chevron-down", -90),
}

# The icons in this library all use a 256-unit viewBox regardless of pixel size.
VIEWBOX = 256.0


def to_highlight(svg: str) -> str:
    """Recolour a whole SVG into the highlight palette."""
    out = svg.replace(BASE_LIGHT, HL_LIGHT).replace(BASE_DARK, HL_DARK)
    # The base palette fades its dark stop out; the highlight palette does not.
    return out.replace(' stop-opacity="0.8"', "")


def split_svg(svg: str):
    """Return (header, defs_inner, body) for a single-defs icon SVG."""
    match = re.search(r"(<svg\b[^>]*>)(.*?)<defs>(.*?)</defs>(.*)</svg>", svg, re.S)
    if not match:
        raise ValueError("unexpected SVG shape: expected one <defs> block inside <svg>")
    header, before_defs, defs_inner, body = match.groups()
    if before_defs.strip():
        raise ValueError("unexpected content before <defs>")
    return header, defs_inner, body


def prefix_ids(fragment: str, prefix: str) -> str:
    """Rename every id= and url(#...) reference so two copies can share one document."""
    fragment = re.sub(r'id="([^"]+)"', lambda m: f'id="{prefix}{m.group(1)}"', fragment)
    return re.sub(r"url\(#([^)]+)\)", lambda m: f"url(#{prefix}{m.group(1)})", fragment)


def make_gauge(svg: str, percent: int) -> str:
    """Stack a base-palette copy over a highlight-palette copy, split at `percent`."""
    header, defs_inner, body = split_svg(svg)
    hl_defs = to_highlight(defs_inner)
    hl_body = to_highlight(body)

    # SVG y grows downward, so "filled from the bottom" means the highlight copy owns
    # everything below this line.
    split_y = VIEWBOX * (1.0 - percent / 100.0)

    hl_defs = prefix_ids(hl_defs, "hl-")
    hl_body = prefix_ids(hl_body, "hl-")

    clips = (
        f'<clipPath id="clip-empty"><rect x="0" y="0" width="{VIEWBOX:g}" '
        f'height="{split_y:g}"/></clipPath>'
        f'<clipPath id="clip-filled"><rect x="0" y="{split_y:g}" width="{VIEWBOX:g}" '
        f'height="{VIEWBOX - split_y:g}"/></clipPath>'
    )

    return (
        f"{header}<defs>{defs_inner}{hl_defs}{clips}</defs>"
        f'<g clip-path="url(#clip-empty)">{body}</g>'
        f'<g clip-path="url(#clip-filled)">{hl_body}</g>'
        f"</svg>\n"
    )


def rotate_svg(svg: str, degrees: int) -> str:
    """Spin the whole icon about the centre of the viewBox."""
    header, defs_inner, body = split_svg(svg)
    centre = VIEWBOX / 2.0
    return (
        f"{header}<defs>{defs_inner}</defs>"
        f'<g transform="rotate({degrees:g},{centre:g},{centre:g})">{body}</g>'
        f"</svg>\n"
    )


def main():
    written = 0

    for name, (source_name, degrees) in ROTATIONS.items():
        source = SVG_DIR / f"{source_name}.svg"
        if not source.exists():
            print(f"  [SKIP] {source_name}.svg not found")
            continue

        target = SVG_DIR / f"{name}.svg"
        target.write_text(rotate_svg(source.read_text(encoding="utf-8"), degrees), encoding="utf-8")
        print(f"  [OK] {target.name}")
        written += 1

    for name in BASES:
        source = SVG_DIR / f"{name}.svg"
        if not source.exists():
            print(f"  [SKIP] {name}.svg not found")
            continue

        svg = source.read_text(encoding="utf-8")

        target = SVG_DIR / f"{name}_highlight.svg"
        target.write_text(to_highlight(svg), encoding="utf-8")
        print(f"  [OK] {target.name}")
        written += 1

        for percent in GAUGES.get(name, []):
            variant = SVG_DIR / f"{name}_{percent}.svg"
            variant.write_text(make_gauge(svg, percent), encoding="utf-8")
            print(f"  [OK] {variant.name}")
            written += 1

    print(f"\nWrote {written} SVGs. Run convert_to_dds.py next.")


if __name__ == "__main__":
    main()
