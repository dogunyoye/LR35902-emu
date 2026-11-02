"""Compare a rom_test_runner screenshot with a DMG reference image.

Usage: compare_screenshot.py SCREENSHOT.ppm REFERENCE.png
Used for dmg-acid2 and the Mealybug Tearoom tests. Pixels are compared by
shade: the screenshot uses the emulator's palette and the references use
greys. Exits 0 when every pixel matches. Requires Pillow.
"""
import sys

from PIL import Image

# Shades 0-3 (lightest to darkest) in the emulator's palette and the references.
EMULATOR_SHADES = {(0xE0, 0xF8, 0xD0): 0, (0x88, 0xC0, 0x70): 1, (0x34, 0x68, 0x56): 2, (0x08, 0x18, 0x20): 3}
REFERENCE_SHADES = {(255, 255, 255): 0, (170, 170, 170): 1, (85, 85, 85): 2, (0, 0, 0): 3}


def shades(path, palette):
    data = Image.open(path).convert("RGB").tobytes()
    return [palette.get(tuple(data[i:i + 3]), -1) for i in range(0, len(data), 3)]


def main():
    shot = shades(sys.argv[1], EMULATOR_SHADES)
    reference = shades(sys.argv[2], REFERENCE_SHADES)
    mismatches = [index for index, (a, b) in enumerate(zip(shot, reference)) if a != b]
    if mismatches or len(shot) != len(reference):
        first = mismatches[0] if mismatches else 0
        print(f"{len(mismatches)} pixels differ; first at x={first % 160} y={first // 160}")
        return 1
    print("Screenshot matches the reference.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
