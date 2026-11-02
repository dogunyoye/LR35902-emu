"""Compare a rom_test_runner screenshot with the dmg-acid2 reference image.

Usage: compare_acid2.py SCREENSHOT.ppm reference-dmg.png
Pixels are compared by shade, so the emulator's palette does not matter.
Exits 0 when every pixel matches. Requires Pillow.
"""
import sys

from PIL import Image


def shades(image):
    # Rank the image's colours by brightness: lightest is shade 0.
    colours = sorted(set(image.getdata()), key=lambda c: -sum(c))
    rank = {colour: index for index, colour in enumerate(colours)}
    return [rank[pixel] for pixel in image.getdata()]


def main():
    shot = Image.open(sys.argv[1]).convert("RGB")
    reference = Image.open(sys.argv[2]).convert("RGB")
    width = shot.size[0]
    mismatches = [
        index for index, (a, b) in enumerate(zip(shades(shot), shades(reference))) if a != b
    ]
    if mismatches:
        first = mismatches[0]
        print(f"{len(mismatches)} pixels differ; first at x={first % width} y={first // width}")
        return 1
    print("Screenshot matches the reference.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
