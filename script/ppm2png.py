#!/usr/bin/env python3
"""ppm2png.py IN.ppm [OUT.png]: the PPM image a window dumps (MYZIG_DUMP)
as a PNG, with the standard library only."""
import struct
import sys
import zlib


def read_ppm(path):
    data = open(path, "rb").read()
    fields, i = [], 0
    while len(fields) < 4:
        while data[i : i + 1].isspace():
            i += 1
        j = i
        while not data[j : j + 1].isspace():
            j += 1
        fields.append(data[i:j])
        i = j
    if fields[0] != b"P6":
        sys.exit(f"{path}: not a binary PPM")
    w, h = int(fields[1]), int(fields[2])
    return w, h, data[i + 1 : i + 1 + w * h * 3]


def write_png(path, w, h, rgb):
    rows = b"".join(b"\0" + rgb[y * w * 3 : (y + 1) * w * 3] for y in range(h))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


if __name__ == "__main__":
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else src.rsplit(".", 1)[0] + ".png"
    write_png(dst, *read_ppm(src))
