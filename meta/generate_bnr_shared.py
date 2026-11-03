#!/usr/bin/env python3
"""
Shared 3DS CBMD (.bnr) Banner & 256x128 Text/Emblem Renderer (`generate_bnr_shared.py`).

Generates:
1. High-contrast 5x7 pixel-font text on `banner.png` (so the 3DS HOME Menu 3D top screen
   prominently displays the game title and subtitle when hovering over the icon).
2. Genuine per-app `banner.bnr` (CBMD container) by:
   - Encoding the 256x128 RGBA `banner` pixel buffer into 3DS PICA200 Morton-swizzled
     RGBA4444 texture tiles (`0x1580..0x11580` inside the CGFX resource),
   - Compressing the 71,040-byte CGFX with Nintendo LZ11 (`0x11` header),
   - Updating the CBMD `cwav_offset` (`+0x84`) and appending the custom BCWAV audio chime.
"""

from __future__ import annotations

import base64
import math
import struct
import zlib
from typing import Dict, List, Sequence, Tuple

CBMD_PREFIX_ZLIB_B64 = (
    "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"
)



MORTON_8X8 = [
    0,  1,  4,  5, 16, 17, 20, 21,
    2,  3,  6,  7, 18, 19, 22, 23,
    8,  9, 12, 13, 24, 25, 28, 29,
   10, 11, 14, 15, 26, 27, 30, 31,
   32, 33, 36, 37, 48, 49, 52, 53,
   34, 35, 38, 39, 50, 51, 54, 55,
   40, 41, 44, 45, 56, 57, 60, 61,
   42, 43, 46, 47, 58, 59, 62, 63,
]

# 5x7 uppercase bitmap font (each entry is 5 column bytes, LSB = top row)
GLYPHS_5X7: Dict[str, Tuple[int, int, int, int, int]] = {
    "A": (0x7E, 0x11, 0x11, 0x11, 0x7E),
    "B": (0x7F, 0x49, 0x49, 0x49, 0x36),
    "C": (0x3E, 0x41, 0x41, 0x41, 0x22),
    "D": (0x7F, 0x41, 0x41, 0x22, 0x1C),
    "E": (0x7F, 0x49, 0x49, 0x49, 0x41),
    "F": (0x7F, 0x09, 0x09, 0x09, 0x01),
    "G": (0x3E, 0x41, 0x49, 0x49, 0x7A),
    "H": (0x7F, 0x08, 0x08, 0x08, 0x7F),
    "I": (0x00, 0x41, 0x7F, 0x41, 0x00),
    "J": (0x20, 0x40, 0x41, 0x3F, 0x01),
    "K": (0x7F, 0x08, 0x14, 0x22, 0x41),
    "L": (0x7F, 0x40, 0x40, 0x40, 0x40),
    "M": (0x7F, 0x02, 0x0C, 0x02, 0x7F),
    "N": (0x7F, 0x04, 0x08, 0x10, 0x7F),
    "O": (0x3E, 0x41, 0x41, 0x41, 0x3E),
    "P": (0x7F, 0x09, 0x09, 0x09, 0x06),
    "Q": (0x3E, 0x41, 0x51, 0x21, 0x5E),
    "R": (0x7F, 0x09, 0x19, 0x29, 0x46),
    "S": (0x46, 0x49, 0x49, 0x49, 0x31),
    "T": (0x01, 0x01, 0x7F, 0x01, 0x01),
    "U": (0x3F, 0x40, 0x40, 0x40, 0x3F),
    "V": (0x1F, 0x20, 0x40, 0x20, 0x1F),
    "W": (0x3F, 0x40, 0x38, 0x40, 0x3F),
    "X": (0x63, 0x14, 0x08, 0x14, 0x63),
    "Y": (0x07, 0x08, 0x70, 0x08, 0x07),
    "Z": (0x61, 0x51, 0x49, 0x45, 0x43),
    "0": (0x3E, 0x51, 0x49, 0x45, 0x3E),
    "1": (0x00, 0x42, 0x7F, 0x40, 0x00),
    "2": (0x42, 0x61, 0x51, 0x49, 0x46),
    "3": (0x21, 0x41, 0x45, 0x4B, 0x31),
    "4": (0x18, 0x14, 0x12, 0x7F, 0x10),
    "5": (0x27, 0x45, 0x45, 0x45, 0x39),
    "6": (0x3C, 0x4A, 0x49, 0x49, 0x30),
    "7": (0x01, 0x71, 0x09, 0x05, 0x03),
    "8": (0x36, 0x49, 0x49, 0x49, 0x36),
    "9": (0x06, 0x49, 0x49, 0x29, 0x1E),
    "-": (0x08, 0x08, 0x08, 0x08, 0x08),
    "+": (0x08, 0x08, 0x3E, 0x08, 0x08),
    ":": (0x00, 0x36, 0x36, 0x00, 0x00),
    ".": (0x00, 0x60, 0x60, 0x00, 0x00),
    "/": (0x20, 0x10, 0x08, 0x04, 0x02),
    "&": (0x36, 0x49, 0x55, 0x22, 0x50),
    " ": (0x00, 0x00, 0x00, 0x00, 0x00),
}


def draw_banner_text_centered(
    buf: bytearray,
    width: int,
    height: int,
    y: int,
    text: str,
    scale: int,
    fg_rgba: Tuple[int, int, int, int],
    shadow_rgba: Tuple[int, int, int, int] = (8, 10, 18, 255),
) -> None:
    text_u = text.upper()
    char_advance = 6 * scale
    total_w = len(text_u) * char_advance - scale
    start_x = max(0, (width - total_w) // 2)

    for pass_idx, (ox, oy, color) in enumerate(((scale, scale, shadow_rgba), (0, 0, fg_rgba))):
        cx = start_x + ox
        cy = y + oy
        for ch in text_u:
            cols = GLYPHS_5X7.get(ch, GLYPHS_5X7[" "])
            for c_idx, col_bits in enumerate(cols):
                for r_idx in range(7):
                    if (col_bits >> r_idx) & 1:
                        for sy in range(scale):
                            for sx in range(scale):
                                px = cx + c_idx * scale + sx
                                py = cy + r_idx * scale + sy
                                if 0 <= px < width and 0 <= py < height:
                                    idx = (py * width + px) * 4
                                    buf[idx : idx + 4] = bytes(color)
            cx += char_advance


def _lz11_decompress(data: bytes) -> bytes:
    assert data[0] == 0x11
    out_len = data[1] | (data[2] << 8) | (data[3] << 16)
    out = bytearray()
    idx = 4
    while len(out) < out_len and idx < len(data):
        flags = data[idx]
        idx += 1
        for bit in range(7, -1, -1):
            if len(out) >= out_len or idx >= len(data):
                break
            if not (flags & (1 << bit)):
                out.append(data[idx])
                idx += 1
            else:
                b1 = data[idx]
                b2 = data[idx + 1]
                idx += 2
                indicator = b1 >> 4
                if indicator == 0:
                    b3 = data[idx]
                    idx += 1
                    length = (((b1 & 0x0F) << 4) | (b2 >> 4)) + 0x11
                    disp = (((b2 & 0x0F) << 8) | b3) + 1
                elif indicator == 1:
                    b3 = data[idx]
                    b4 = data[idx + 1]
                    idx += 2
                    length = (((b1 & 0x0F) << 12) | (b2 << 4) | (b3 >> 4)) + 0x111
                    disp = (((b3 & 0x0F) << 8) | b4) + 1
                else:
                    length = indicator + 1
                    disp = (((b1 & 0x0F) << 8) | b2) + 1
                for _ in range(length):
                    out.append(out[-disp])
    return bytes(out)


def _lz11_compress(raw: bytes) -> bytes:
    """High-ratio Nintendo LZ11 encoder with 4KB hash-chain lookback and indicator-1 (up to 65808-byte) matches."""
    n = len(raw)
    out = bytearray([0x11, n & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF])
    # Direct 2-byte prefix hash table storing recent positions (capped to 16 entries per bucket for speed)
    buckets: Dict[int, List[int]] = {}
    i = 0
    while i < n:
        flag_pos = len(out)
        out.append(0)
        flags = 0
        for bit in range(7, -1, -1):
            if i >= n:
                break
            best_len = 0
            best_disp = 0
            max_l = min(0x10110, n - i)
            if max_l >= 3 and i >= 1:
                key = (raw[i] << 8) | raw[i + 1]
                cand_list = buckets.get(key)
                if cand_list:
                    for pos in reversed(cand_list[-16:]):
                        disp = i - pos
                        if disp > 0x1000:
                            break
                        if raw[i + 2] == raw[pos + 2]:
                            l = 3
                            while l < max_l and raw[i + l] == raw[pos + l]:
                                l += 1
                            if l > best_len:
                                best_len = l
                                best_disp = disp
                                if best_len >= 512 or best_len == max_l:
                                    break
                # Always check disp=2 and disp=4 fast RLE paths for 16bpp/32bpp solid fills
                for disp in (2, 4):
                    if i >= disp and raw[i] == raw[i - disp] and raw[i + 1] == raw[i + 1 - disp]:
                        l = 2
                        while l < max_l and raw[i + l] == raw[i + l - disp]:
                            l += 1
                        if l > best_len:
                            best_len = l
                            best_disp = disp

            if best_len >= 3:
                flags |= 1 << bit
                d = best_disp - 1
                if best_len <= 0x10:
                    l_code = best_len - 1
                    out.append((l_code << 4) | ((d >> 8) & 0x0F))
                    out.append(d & 0xFF)
                elif best_len <= 0x110:
                    l_code = best_len - 0x11
                    out.append((l_code >> 4) & 0x0F)
                    out.append(((l_code & 0x0F) << 4) | ((d >> 8) & 0x0F))
                    out.append(d & 0xFF)
                else:
                    l_code = best_len - 0x111
                    out.append(0x10 | ((l_code >> 12) & 0x0F))
                    out.append((l_code >> 4) & 0xFF)
                    out.append(((l_code & 0x0F) << 4) | ((d >> 8) & 0x0F))
                    out.append(d & 0xFF)
                # Index stepped positions every 2 bytes
                step_end = min(n - 2, i + best_len)
                for p in range(i, step_end, 2):
                    k = (raw[p] << 8) | raw[p + 1]
                    buckets.setdefault(k, []).append(p)
                i += best_len
            else:
                if i + 2 <= n:
                    k = (raw[i] << 8) | raw[i + 1]
                    buckets.setdefault(k, []).append(i)
                out.append(raw[i])
                i += 1
        out[flag_pos] = flags
    while (0x88 + len(out)) % 0x20 != 0:
        out.append(0)
    return bytes(out)


def encode_rgba_to_3ds_rgba4444_morton(rgba_bytes: bytes, width: int = 256, height: int = 128) -> bytes:
    tex = bytearray(width * height * 2)
    tiles_x = width // 8
    for y in range(height):
        yy = y  # 3DS banner CGFX UV mapping uses top-to-bottom tile row order
        tile_y = yy // 8
        sub_y = yy % 8
        for x in range(width):
            tile_x = x // 8
            sub_x = x % 8
            src_idx = (y * width + x) * 4
            r = rgba_bytes[src_idx] >> 4
            g = rgba_bytes[src_idx + 1] >> 4
            b = rgba_bytes[src_idx + 2] >> 4
            a = rgba_bytes[src_idx + 3] >> 4
            val = (r << 12) | (g << 8) | (b << 4) | a
            dst_pixel = (tile_y * tiles_x + tile_x) * 64 + MORTON_8X8[sub_y * 8 + sub_x]
            struct.pack_into("<H", tex, dst_pixel * 2, val)
    return bytes(tex)


def patch_smdh_for_cia(smdh_path: str) -> None:
    """Patch smdhtool output so the 3DS HOME Menu accepts the CIA icon/metadata (+0x2008 ratings, +0x2018 region, +0x2028 flags)."""
    with open(smdh_path, "rb") as f:
        buf = bytearray(f.read())
    if len(buf) != 0x36C0 or buf[:4] != b"SMDH":
        return
    # Clear age ratings to 0x00 (instead of smdhtool's 0xA0)
    buf[0x2008:0x2018] = b"\x00" * 16
    # Standard region-free bitmask (0x7FFFFFFF) and HOME Menu visible/nosave/recordusage flags (0x00000105)
    struct.pack_into("<I", buf, 0x2018, 0x7FFFFFFF)
    struct.pack_into("<I", buf, 0x2028, 0x00000105)
    with open(smdh_path, "wb") as f:
        f.write(buf)


def write_cbmd_bnr(
    path: str,
    freqs: Sequence[float] = (523.25, 659.25, 783.99),
    banner_rgba: bytes | None = None,
) -> None:
    prefix = bytearray(zlib.decompress(base64.b64decode(CBMD_PREFIX_ZLIB_B64)))
    orig_cwav_off = struct.unpack_from("<I", prefix, 0x84)[0]

    if banner_rgba is not None and len(banner_rgba) == 256 * 128 * 4:
        cgfx_raw = bytearray(_lz11_decompress(bytes(prefix[0x88:orig_cwav_off])))
        cgfx_raw[0x1580 : 0x1580 + 65536] = encode_rgba_to_3ds_rgba4444_morton(banner_rgba, 256, 128)
        comp_cgfx = _lz11_compress(bytes(cgfx_raw))
        cwav_header_part = bytes(prefix[orig_cwav_off:])
        cbmd_hdr = bytearray(prefix[:0x88])
        new_cwav_off = 0x88 + len(comp_cgfx)
        struct.pack_into("<I", cbmd_hdr, 0x84, new_cwav_off)
        prefix = cbmd_hdr + bytearray(comp_cgfx) + bytearray(cwav_header_part)

    sample_rate = 22050
    duration_s = 0.8
    n_samples = int(sample_rate * duration_s)
    frames = bytearray()
    for i in range(n_samples):
        t = i / sample_rate
        freq = freqs[0] if t < 0.25 else (freqs[1] if t < 0.5 else freqs[2])
        env = max(0.0, 1.0 - (t / duration_s))
        val = int(12000 * env * math.sin(2.0 * math.pi * freq * t))
        frames.extend(struct.pack("<h", val))
    with open(path, "wb") as f:
        f.write(prefix)
        f.write(bytes(frames))
