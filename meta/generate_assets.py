#!/usr/bin/env python3
"""Original FoldQuest procedural clamshell icon (48x48), authored in this app.
No external images, model templates, banner resources or sampled audio.
Usage: generate_assets.py OUTDIR | --patch-smdh FILE
"""
import os
import struct
import sys
import zlib

def write_png(path, width, height, rgba):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        rows.extend(rgba[y * width * 4:(y + 1) * width * 4])
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))


def fold_pixel(x, y, w, h):
    """A 'clamshell': two panels split by a crease; the lower panel is shifted/shaded."""
    mid = h // 2
    if abs(y - mid) <= 1:
        return (255, 230, 90, 255)            # crease
    if y < mid:
        return (60 + x * 40 // w, 90, 170, 255) if (x // 6 + y // 6) % 2 else (50 + x * 40 // w, 80, 150, 255)
    return (200, 110 + x * 40 // w, 60, 255) if (x // 6 + y // 6) % 2 else (180, 95 + x * 40 // w, 50, 255)


def icon(path):
    w = h = 48
    buf = bytearray()
    for y in range(h):
        for x in range(w):
            edge = x < 2 or y < 2 or x >= w - 2 or y >= h - 2
            buf += bytes((250, 250, 250, 255) if edge else fold_pixel(x, y, w, h))
    write_png(path, w, h, bytes(buf))


def patch_smdh_for_cia(smdh_path):
    """Retain the shared encoder's metadata-only patch, without any binary assets.

    Clear ratings; keep the existing region-free mask and visible/3D/usage flags.
    These public SMDH fields are not a banner or a boot-logo resource.
    """
    with open(smdh_path, "rb") as f:
        buf = bytearray(f.read())
    if len(buf) != 0x36C0 or buf[:4] != b"SMDH":
        raise ValueError("bad SMDH size/magic")
    buf[0x2008:0x2018] = bytes(16)
    struct.pack_into("<I", buf, 0x2018, 0x7FFFFFFF)
    struct.pack_into("<I", buf, 0x2028, 0x00000105)
    with open(smdh_path, "wb") as f:
        f.write(buf)


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--patch-smdh":
        patch_smdh_for_cia(sys.argv[2])
        return
    out = sys.argv[1] if len(sys.argv) == 2 else None
    if not out:
        sys.exit("usage: generate_assets.py OUTDIR | --patch-smdh FILE")
    os.makedirs(out, exist_ok=True)
    icon(os.path.join(out, "icon.png"))
    print("Generated original FoldQuest icon.png only in", out)


if __name__ == "__main__":
    main()
