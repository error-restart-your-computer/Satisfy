#!/usr/bin/env python3
"""Dump the final frame of an .ans capture as readable ASCII art.

usage: ans2txt.py input.ans
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ans2png import read_cells  # noqa: E402

RAMP = " .:-=+*#%@"


def lum(c):
    return (0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]) / 255.0


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    with open(sys.argv[1], 'rb') as fh:
        data = fh.read()
    grid, rows, cols = read_cells(data)
    for r in range(rows):
        line = []
        for c in range(cols):
            cell = grid.get((r, c))
            if cell is None:
                line.append(' ')
                continue
            ch, fg, bg = cell
            if ch == '▀':                      # half-block pixel pair
                l = (lum(fg) + lum(bg)) * 0.5
                line.append(RAMP[min(9, int(l * 9.99))])
            elif ch == ' ':                    # solid-colour cell
                line.append(RAMP[min(9, int(lum(bg) * 9.99))])
            else:
                line.append(ch)
        print(''.join(line))
    return 0


if __name__ == '__main__':
    sys.exit(main())
