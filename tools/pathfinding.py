#!/usr/bin/env python3
"""Reader for Refractor AI path-finding maps, level 0 (<Map>/Pathfinding/<name>level0map.raw). Stdlib only.

Spec: docs/formats/pathfinding.md

Usage: pathfinding.py <BF1942_DIR> <mod> <level> <file>   print size and blocked fraction
"""
import struct
import sys

from con import load_mod

FULL = 0xFFFFFFFF


def parse(d):
    """Level-0 map -> (n, bytearray of n*n cells, 1 = blocked), row = z, column = x."""
    if len(d) < 32:
        raise ValueError("path map: truncated header")
    a, a2, b, level = struct.unpack_from("<4I", d, 0)
    if a != a2 or level != 0 or a > 8 or b > 10 or a + b > 13:
        raise ValueError(f"path map: unsupported header {a} {a2} {b} {level}")
    tiles, cells = 1 << a, 1 << b
    n, words = tiles * cells, cells * cells // 32
    grid = bytearray(n * n)
    o = 32
    for t in range(tiles * tiles):
        if o + 4 > len(d):
            raise ValueError("path map: truncated")
        (marker,) = struct.unpack_from("<I", d, o)
        o += 4
        tx, tz = t % tiles, t // tiles
        if marker == FULL:  # cells x cells bits, rows of cells / 32 words, bit 0 = lowest x
            if o + words * 4 > len(d):
                raise ValueError("path map: truncated tile")
            ws = struct.unpack_from(f"<{words}I", d, o)
            o += words * 4
            for r in range(cells):
                for wi in range(cells // 32):
                    w = ws[r * (cells // 32) + wi]
                    base = (tz * cells + r) * n + tx * cells + wi * 32
                    for bit in range(32):
                        if w >> bit & 1:
                            grid[base + bit] = 1
        elif marker == 1:  # whole tile blocked
            for r in range(cells):
                row = (tz * cells + r) * n + tx * cells
                grid[row:row + cells] = b"\x01" * cells
        elif marker != 0:
            raise ValueError(f"path map: unknown tile marker {marker:#x}")
    if o != len(d):
        raise ValueError("path map: trailing data")
    return n, grid


def main(argv):
    if len(argv) != 5:
        print(__doc__)
        return 2
    vm = load_mod(argv[1], argv[2])
    n, grid = parse(vm.vfs.read(f"bf1942/levels/{argv[3].lower()}/pathfinding/{argv[4].lower()}"))
    print(f"{n} x {n} cells, {sum(grid) / len(grid):.1%} blocked")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
