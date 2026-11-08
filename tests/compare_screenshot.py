"""Compare a rom_test_runner screenshot with a reference image.

Usage: compare_screenshot.py [--rgb] SCREENSHOT.ppm REFERENCE.png
Used for dmg-acid2, cgb-acid2, and the Mealybug Tearoom tests. By default
pixels are compared by shade: the screenshot uses the emulator's DMG palette
and the references use greys. With --rgb, for Game Boy Color references,
colours must match exactly. Exits 0 when every pixel matches. Requires Pillow.
"""
import sys

from PIL import Image

# Shades 0-3 (lightest to darkest) in the emulator's palette and the references.
EMULATOR_SHADES = {(0xE0, 0xF8, 0xD0): 0, (0x88, 0xC0, 0x70): 1, (0x34, 0x68, 0x56): 2, (0x08, 0x18, 0x20): 3}
REFERENCE_SHADES = {(255, 255, 255): 0, (170, 170, 170): 1, (85, 85, 85): 2, (0, 0, 0): 3}


def shades(path, palette):
    data = Image.open(path).convert("RGB").tobytes()
    return [palette.get(tuple(data[i:i + 3]), -1) for i in range(0, len(data), 3)]


def colors(path):
    data = Image.open(path).convert("RGB").tobytes()
    return [tuple(data[i:i + 3]) for i in range(0, len(data), 3)]


def main():
    # --rgb compares exact colours, for Game Boy Color references.
    args = sys.argv[1:]
    if args[0] == "--rgb":
        shot, reference = colors(args[1]), colors(args[2])
        mismatches = [index for index, (a, b) in enumerate(zip(shot, reference)) if a != b]
    else:
        shot = shades(args[0], EMULATOR_SHADES)
        reference = shades(args[1], REFERENCE_SHADES)
        # Colours outside either palette (-1) never match.
        mismatches = [index for index, (a, b) in enumerate(zip(shot, reference)) if a != b or a < 0]
    if mismatches or len(shot) != len(reference):
        first = mismatches[0] if mismatches else 0
        print(f"{len(mismatches)} pixels differ; first at x={first % 160} y={first // 160}")
        return 1
    print("Screenshot matches the reference.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
