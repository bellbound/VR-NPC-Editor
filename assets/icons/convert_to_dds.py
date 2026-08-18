"""
Icon DDS Converter & Deployer

Adapted from skse/VR-Editor/assets/icons/convert_to_dds.py. Two changes:
  - deploys to papyrus/mods/VR NPC Editor/textures/VRNPCEditor
  - refuses any icon that is not power-of-two square, because 3DUI crashes the game
    on load when handed one, and a crash on load is a miserable way to find out

Pipeline:
  svg/*.svg  --ImageMagick-->  pngs/*.png  --texconv-->  dds/*.dds  --copy--> mod

SVG -> PNG is skipped when the PNG already exists, so hand-tweaked PNGs survive.
Delete the PNG to re-render it from its SVG.

Requirements:
    - magick.exe (ImageMagick) on PATH, or at MAGICK_FALLBACK below
    - texconv.exe (DirectXTex) on PATH

Usage:
    python make_variants.py     # regenerate derived SVGs first
    python convert_to_dds.py
"""

import shutil
import struct
import subprocess
from pathlib import Path

_SCRIPT_DIR = Path(__file__).parent.resolve()
_PROJECT_ROOT = _SCRIPT_DIR.parents[3]  # skse/<mod>/assets/icons -> project root
MOD_TEXTURES_DIR = _PROJECT_ROOT / "papyrus" / "mods" / "VR NPC Editor" / "textures" / "VRNPCEditor"

MAGICK_FALLBACK = r"C:\Program Files\ImageMagick-7.1.2-Q16\magick.exe"

# 3DUI renders these on projectiles; anything that is not a power-of-two square is a
# hard crash on load rather than a visual glitch.
ICON_SIZE = 512


def find_magick() -> str:
    found = shutil.which("magick")
    if found:
        return found
    if Path(MAGICK_FALLBACK).exists():
        return MAGICK_FALLBACK
    return ""


def png_dimensions(path: Path):
    """Read width/height straight out of the PNG IHDR."""
    with path.open("rb") as handle:
        header = handle.read(24)
    if len(header) < 24 or header[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return struct.unpack(">II", header[16:24])


def convert_svgs_to_pngs(svg_dir: Path, png_dir: Path) -> int:
    svg_dir.mkdir(parents=True, exist_ok=True)
    png_dir.mkdir(parents=True, exist_ok=True)

    svg_files = sorted(svg_dir.glob("*.svg"))
    if not svg_files:
        print("  No SVG files found")
        return 0

    magick = find_magick()
    if not magick:
        print("  [ERR] magick not found on PATH")
        print("    Download from: https://imagemagick.org/script/download.php")
        return 0

    converted = 0
    skipped = 0

    for svg_path in svg_files:
        png_path = png_dir / (svg_path.stem + ".png")
        if png_path.exists():
            skipped += 1
            continue

        result = subprocess.run(
            [
                magick,
                "-background", "none",
                str(svg_path),
                "-resize", f"{ICON_SIZE}x{ICON_SIZE}",
                # Pad rather than stretch, so a non-square source keeps its shape.
                "-gravity", "center",
                "-extent", f"{ICON_SIZE}x{ICON_SIZE}",
                str(png_path),
            ],
            capture_output=True,
            text=True,
        )

        if result.returncode == 0:
            print(f"  [OK] {svg_path.name} -> {png_path.name}")
            converted += 1
        else:
            print(f"  [ERR] {svg_path.name} - ImageMagick: {result.stderr.strip()}")

    if skipped:
        print(f"  Skipped {skipped} SVGs (PNG already exists)")
    return converted


def convert_pngs_to_dds(png_dir: Path, dds_dir: Path) -> int:
    dds_dir.mkdir(parents=True, exist_ok=True)
    png_files = sorted(png_dir.glob("*.png"))

    if not png_files:
        print(f"  No PNG files found in {png_dir}")
        return 0

    converted = 0
    for png_path in png_files:
        size = png_dimensions(png_path)
        if size is None:
            print(f"  [ERR] {png_path.name} - not a readable PNG, refusing to convert")
            continue

        width, height = size
        if width != height or width & (width - 1):
            print(f"  [ERR] {png_path.name} - {width}x{height} is not a power-of-two "
                  f"square; 3DUI would crash the game on load. Skipped.")
            continue

        result = subprocess.run(
            ["texconv", "-f", "BC7_UNORM", "-y", "-o", str(dds_dir), str(png_path)],
            capture_output=True,
            text=True,
        )

        if result.returncode == 0:
            print(f"  [OK] {png_path.name} -> {png_path.stem}.dds")
            converted += 1
        else:
            print(f"  [ERR] {png_path.name} - texconv: {result.stderr.strip()}")

    return converted


def copy_to_mod_folder(dds_dir: Path, mod_dir: Path) -> int:
    mod_dir.mkdir(parents=True, exist_ok=True)

    copied = 0
    for dds_path in sorted(dds_dir.glob("*.dds")):
        shutil.copy2(dds_path, mod_dir / dds_path.name)
        copied += 1

    print(f"  {copied} DDS files -> {mod_dir}")
    return copied


def main():
    svg_dir = _SCRIPT_DIR / "svg"
    png_dir = _SCRIPT_DIR / "pngs"
    dds_dir = _SCRIPT_DIR / "dds"

    print("=" * 60)
    print("VR NPC Editor - icon converter")
    print("=" * 60)

    print("\n[1/3] SVG -> PNG")
    print("-" * 40)
    svg_count = convert_svgs_to_pngs(svg_dir, png_dir)

    print("\n[2/3] PNG -> DDS")
    print("-" * 40)
    dds_count = convert_pngs_to_dds(png_dir, dds_dir)

    print("\n[3/3] Deploy")
    print("-" * 40)
    copy_count = copy_to_mod_folder(dds_dir, MOD_TEXTURES_DIR)

    print("\n" + "=" * 60)
    print(f"Rendered {svg_count} SVGs, wrote {dds_count} DDS, deployed {copy_count}.")
    print("=" * 60)


if __name__ == "__main__":
    main()
