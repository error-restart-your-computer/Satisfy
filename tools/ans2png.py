#!/usr/bin/env python3
"""Replay an ANSI frame stream (.ans) and render the final frame to a PNG.

usage: ans2png.py input.ans output.png [cell_w] [cell_h]
"""
import sys
import struct


def read_cells(data):
    grid = {}
    r = c = 0
    fg = (230, 230, 230)
    bg = (0, 0, 0)
    maxr = maxc = 0
    i = 0
    n = len(data)
    while i < n:
        b = data[i]
        if b == 0x1B:
            if i + 1 >= n:
                break
            if data[i + 1] != ord('['):
                i += 2
                continue
            j = i + 2
            while j < n and (48 <= data[j] <= 57 or data[j] in (59, 63, 60, 62, 63)):
                j += 1
            if j >= n:
                break
            fin = chr(data[j])
            params = data[i + 2:j].decode('ascii', 'replace')
            i = j + 1
            if fin == 'H':
                parts = params.split(';')
                rr = int(parts[0]) if parts and parts[0] else 1
                cc = int(parts[1]) if len(parts) > 1 and parts[1] else 1
                r, c = rr - 1, cc - 1
            elif fin == 'C':
                c += int(params) if params else 1
            elif fin == 'D':
                c -= int(params) if params else 1
            elif fin == 'A':
                r -= int(params) if params else 1
            elif fin == 'B':
                r += int(params) if params else 1
            elif fin == 'J':
                grid.clear()
            elif fin == 'm':
                if params in ('', '0'):
                    fg = (230, 230, 230)
                    bg = (0, 0, 0)
                else:
                    toks = params.split(';')
                    k = 0
                    while k < len(toks):
                        t = toks[k]
                        if t == '38' and k + 4 < len(toks) and toks[k + 1] == '2':
                            fg = (int(toks[k + 2]), int(toks[k + 3]), int(toks[k + 4]))
                            k += 5
                        elif t == '48' and k + 4 < len(toks) and toks[k + 1] == '2':
                            bg = (int(toks[k + 2]), int(toks[k + 3]), int(toks[k + 4]))
                            k += 5
                        else:
                            k += 1
            continue
        # decode one utf-8 character
        if b < 0x80:
            ch = chr(b)
            i += 1
        elif b < 0xE0:
            ch = data[i:i + 2].decode('utf-8', 'replace')
            i += 2
        elif b < 0xF0:
            ch = data[i:i + 3].decode('utf-8', 'replace')
            i += 3
        else:
            ch = data[i:i + 4].decode('utf-8', 'replace')
            i += 4
        if ch in ('\r', '\n', '\x07'):
            continue
        grid[(r, c)] = (ch, fg, bg)
        if r > maxr:
            maxr = r
        if c > maxc:
            maxc = c
        c += 1
    return grid, maxr + 1, maxc + 1


def render(grid, rows, cols, cw, chh, outpath):
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        render_ppm(grid, rows, cols, cw, chh, outpath.replace('.png', '.ppm'))
        return
    img = Image.new('RGB', (cols * cw, rows * chh), (0, 0, 0))
    d = ImageDraw.Draw(img)
    try:
        font = ImageFont.load_default(size=int(chh * 0.75))
    except TypeError:
        font = ImageFont.load_default()
    for (rr, cc), (ch, f, b) in grid.items():
        if rr < 0 or cc < 0 or rr >= rows or cc >= cols:
            continue
        x, y = cc * cw, rr * chh
        if ch == '▀':                      # upper half block: fg=top, bg=bottom
            d.rectangle([x, y, x + cw - 1, y + chh // 2 - 1], fill=f)
            d.rectangle([x, y + chh // 2, x + cw - 1, y + chh - 1], fill=b)
        else:
            d.rectangle([x, y, x + cw - 1, y + chh - 1], fill=b)
            if ch not in (' ',):
                d.text((x + (cw - 6) // 2, y + (chh - 11) // 2), ch, fill=f, font=font)
    img.save(outpath)
    print('%s: %dx%d cells -> %s' % (outpath, cols, rows, img.size))


def render_ppm(grid, rows, cols, cw, chh, outpath):
    img = bytearray(cols * cw * rows * chh * 3)

    def px(x, y, col):
        if 0 <= x < cols * cw and 0 <= y < rows * chh:
            o = (y * cols * cw + x) * 3
            img[o] = col[0]
            img[o + 1] = col[1]
            img[o + 2] = col[2]

    for (rr, cc), (ch, f, b) in grid.items():
        if rr < 0 or cc < 0 or rr >= rows or cc >= cols:
            continue
        x0, y0 = cc * cw, rr * chh
        if ch == '▀':
            for y in range(y0, y0 + chh // 2):
                for x in range(x0, x0 + cw):
                    px(x, y, f)
            for y in range(y0 + chh // 2, y0 + chh):
                for x in range(x0, x0 + cw):
                    px(x, y, b)
        else:
            for y in range(y0, y0 + chh):
                for x in range(x0, x0 + cw):
                    px(x, y, b)
    with open(outpath, 'wb') as fh:
        fh.write(b'P6\n%d %d\n255\n' % (cols * cw, rows * chh))
        fh.write(bytes(img))
    print('%s: %dx%d cells -> %s (no PIL)' % (outpath, cols, rows, (cols * cw, rows * chh)))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    with open(sys.argv[1], 'rb') as fh:
        data = fh.read()
    grid, rows, cols = read_cells(data)
    cw = int(sys.argv[3]) if len(sys.argv) > 3 else 10
    chh = int(sys.argv[4]) if len(sys.argv) > 4 else 20
    render(grid, rows, cols, cw, chh, sys.argv[2])
    return 0


if __name__ == '__main__':
    sys.exit(main())
