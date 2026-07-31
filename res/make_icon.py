#!/usr/bin/env python3
"""
Generates res/app.ico.

The .ico is committed, so building LanTaskmgr never needs Python. This script
only exists so the icon is reproducible instead of an opaque binary blob.

    python res/make_icon.py

Design: the same mark the web UI draws in CSS -- a rounded accent-blue tile
with three white bars, like a load meter.
"""

import os
import struct
import zlib

ACCENT = (0x2B, 0x6C, 0xF6)   # RGB, matches --accent in web/app.css
BAR = (0xFF, 0xFF, 0xFF)
SS = 4                        # supersampling factor for antialiasing

# Bar geometry as fractions of the canvas: (x, width, height)
BARS = ((0.26, 0.11, 0.30), (0.445, 0.11, 0.52), (0.63, 0.11, 0.40))
BAR_BOTTOM = 0.74             # baseline, measured from the top


def render(size):
    """Returns a size*size list of (r, g, b, a) tuples."""
    n = size * SS
    radius = n * 0.22
    inset = n * 0.06
    lo, hi = inset, n - inset

    # Bars in supersampled coordinates.
    bars = []
    base = n * BAR_BOTTOM
    for fx, fw, fh in BARS:
        x0 = n * fx
        bars.append((x0, x0 + n * fw, base - n * fh, base, n * fw * 0.30))

    def rounded(px, py, x0, y0, x1, y1, r):
        """Inside test for a rounded rectangle."""
        cx = min(max(px, x0 + r), x1 - r)
        cy = min(max(py, y0 + r), y1 - r)
        if x0 <= px <= x1 and y0 <= py <= y1:
            if (px < x0 + r or px > x1 - r) and (py < y0 + r or py > y1 - r):
                return (px - cx) ** 2 + (py - cy) ** 2 <= r * r
            return True
        return False

    out = []
    for y in range(size):
        for x in range(size):
            hits = 0
            bar_hits = 0
            for sy in range(SS):
                py = y * SS + sy + 0.5
                for sx in range(SS):
                    px = x * SS + sx + 0.5
                    if not rounded(px, py, lo, lo, hi, hi, radius):
                        continue
                    hits += 1
                    for bx0, bx1, by0, by1, br in bars:
                        if rounded(px, py, bx0, by0, bx1, by1, br):
                            bar_hits += 1
                            break

            total = SS * SS
            if hits == 0:
                out.append((0, 0, 0, 0))
                continue

            alpha = int(round(255 * hits / total))
            # Blend bar colour over the tile by coverage.
            cover = bar_hits / hits
            rgb = tuple(
                int(round(ACCENT[i] * (1 - cover) + BAR[i] * cover)) for i in range(3)
            )
            out.append((rgb[0], rgb[1], rgb[2], alpha))
    return out


def bmp_entry(pixels, size):
    """32-bit BGRA DIB, bottom-up, with the (unused but mandatory) AND mask."""
    header = struct.pack(
        "<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, size * size * 4, 0, 0, 0, 0
    )
    body = bytearray()
    for y in range(size - 1, -1, -1):
        row = pixels[y * size:(y + 1) * size]
        for r, g, b, a in row:
            body += bytes((b, g, r, a))
    # AND mask: one bit per pixel, rows padded to 4 bytes. Zero = opaque, which
    # is correct because the alpha channel already carries the shape.
    stride = ((size + 31) // 32) * 4
    body += bytes(stride * size)
    return header + bytes(body)


def png_entry(pixels, size):
    raw = bytearray()
    for y in range(size):
        raw.append(0)  # filter: none
        for r, g, b, a in pixels[y * size:(y + 1) * size]:
            raw += bytes((r, g, b, a))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def main():
    # Small sizes as classic DIBs (every shell path understands them), the
    # 256 tile as PNG so the file stays a fraction of the size.
    images = [(s, bmp_entry(render(s), s)) for s in (16, 32, 48)]
    images.append((256, png_entry(render(256), 256)))

    offset = 6 + 16 * len(images)
    directory = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    blob = bytearray()
    for size, data in images:
        directory += struct.pack(
            "<BBBBHHII",
            size if size < 256 else 0,
            size if size < 256 else 0,
            0, 0, 1, 32, len(data), offset,
        )
        blob += data
        offset += len(data)

    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "app.ico")
    with open(path, "wb") as fh:
        fh.write(bytes(directory) + bytes(blob))
    print("wrote %s (%d bytes)" % (path, len(directory) + len(blob)))


if __name__ == "__main__":
    main()
