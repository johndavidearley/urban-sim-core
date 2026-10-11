#!/usr/bin/env python3
"""Check fixed geometry/color probes; optionally compare a directory of reference PPMs."""
import argparse
from pathlib import Path


def read_ppm(path):
    with path.open('rb') as stream:
        if stream.readline().strip() != b'P6':
            raise ValueError(f'{path}: expected binary PPM')
        width, height = map(int, stream.readline().split())
        if stream.readline().strip() != b'255':
            raise ValueError(f'{path}: expected 8-bit color')
        pixels = stream.read()
    if len(pixels) != width * height * 3:
        raise ValueError(f'{path}: incomplete image')
    return width, height, pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('captures', type=Path)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--tolerance', type=int, default=3)
    parser.add_argument('--max-different-fraction', type=float, default=0.01)
    args = parser.parse_args()
    for scene in range(16):
        path = args.captures / f'scene-{scene}.ppm'
        width, height, pixels = read_ppm(path)
        logical_width, logical_height = (1100, 720) if 8 <= scene < 16 else (1280, 800)
        def sample(x, y):
            sx, sy = int(x * width / logical_width), int(y * height / logical_height)
            offset = (sy * width + sx) * 3
            return tuple(pixels[offset:offset + 3])
        for x, expected in [(18, (255, 0, 0)), (38, (128, 0, 127))]:
            actual = sample(x, logical_height - 30)
            if any(abs(a - b) > args.tolerance for a, b in zip(actual, expected)):
                raise AssertionError(f'{path}: color/order probe {x}: {actual}, expected {expected}')
        if args.reference:
            rw, rh, reference = read_ppm(args.reference / path.name)
            if (rw, rh) != (width, height):
                raise AssertionError(f'{path}: reference dimensions differ')
            if pixels == reference:
                continue
            different = sum(abs(pixels[i] - reference[i]) > args.tolerance
                            or abs(pixels[i + 1] - reference[i + 1]) > args.tolerance
                            or abs(pixels[i + 2] - reference[i + 2]) > args.tolerance
                            for i in range(0, len(pixels), 3))
            fraction = different / (width * height)
            if fraction > args.max_different_fraction:
                raise AssertionError(f'{path}: {fraction:.2%} pixels differ beyond tolerance')
    print('16 graphics captures passed geometry/color checks' + (' and reference comparison' if args.reference else ''))


if __name__ == '__main__':
    main()
