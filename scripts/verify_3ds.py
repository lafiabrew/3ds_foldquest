#!/usr/bin/env python3
"""Verify FoldQuest native artifacts beyond magic bytes.

Checks: ARM ELF + defined application/SDK symbols, call-graph reachability from main() (parsed from
the ELF disassembly), libctru/citro2d link evidence in the linker map, SMDH text/flags/age/region,
3DSX-embedded SMDH, CIA TMD identity/content flags/hash and actual CIA/CXI NCCH identity,
ExeFS code/icon integrity and explicit absence of optional banner/logo resources.
--negative-control: reject a same-architecture stub launcher (compiled and linked for ARM with
libctru, no game code) and a mutated copy of the real disassembly for the missing game path,
NOT for a wrong machine type; reject specific packaging mutations in the actual production CIA."""
import argparse
import hashlib
import os
import re
import struct
import sys
import tempfile

TARGET = "3ds_foldquest"
TITLE_ID = 0x00040000_00D3F700       # 0x0D3F7 << 8 in the low word
PRODUCT_CODE = b"CTR-P-FOLD"
REQUIRED_SYMBOLS = ["main", "fq_app_frame", "fq_app_lifecycle", "fq_app_draw", "fq_app_shutdown", "fq_move",
                    "fq_resume", "fq_suspend", "fq_log_export", "nls_commit", "fq_apt_lifecycle_init",
                    "aptHook", "aptMainLoop", "hidTouchRead", "FSUSER_OpenArchive", "FSUSER_RenameFile"]
# Must be reachable from main() through direct calls/branches in the disassembly.
REQUIRED_REACHABLE = ["fq_app_frame", "fq_app_lifecycle", "fq_app_draw", "fq_app_shutdown", "fq_resume",
                      "fq_suspend", "fq_move", "fq_log_export", "nls_commit", "fq_apt_lifecycle_init",
                      "aptHook", "aptMainLoop", "hidTouchRead", "FSUSER_OpenArchive"]


def elf_symbols(path):
    data = open(path, "rb").read()
    errors = []
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        return None, ["not a 32-bit little-endian ELF"], set()
    machine = struct.unpack_from("<H", data, 18)[0]
    if machine != 40:
        errors.append(f"wrong machine {machine} (expected ARM 40)")
    shoff, = struct.unpack_from("<I", data, 32)
    shentsize, shnum = struct.unpack_from("<HH", data, 46)
    sections = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shentsize) for i in range(shnum)]
    defined = set()
    for name, typ, flags, addr, off, size, link, info, align, entsize in sections:
        if typ != 2:  # SHT_SYMTAB
            continue
        stroff = sections[link][4]
        for i in range(size // 16):
            st_name, value, sz, info_b, other, shndx = struct.unpack_from("<IIIBBH", data, off + i * 16)
            if shndx == 0 or (info_b & 0xF) != 2:  # defined FUNC only
                continue
            end = data.index(b"\0", stroff + st_name)
            defined.add(data[stroff + st_name:end].decode())
    return machine, errors, defined


def parse_graph(dis_text):
    graph, current = {}, None
    head = re.compile(r"^[0-9a-f]+ <([^>]+)>:$")
    branch = re.compile(r"^\s+[0-9a-f]+:\s+[0-9a-f]+\s+(?:bl|b|blx)[a-z]{0,2}\s+[0-9a-f]+ <([^>+]+)(?:\+0x[0-9a-f]+)?>")
    for line in dis_text.splitlines():
        m = head.match(line)
        if m:
            current = m.group(1)
            graph.setdefault(current, set())
            continue
        m = branch.match(line)
        if m and current:
            graph[current].add(m.group(1))
    return graph


def reachable(graph, root="main"):
    seen, todo = set(), [root]
    while todo:
        n = todo.pop()
        if n in seen:
            continue
        seen.add(n)
        todo.extend(graph.get(n, ()))
    return seen


def check_elf(elf, dis_text, mapfile):
    machine, errors, defined = elf_symbols(elf)
    if machine is None:
        return errors
    for sym in REQUIRED_SYMBOLS:
        if sym not in defined:
            errors.append(f"missing defined symbol {sym}")
    graph = parse_graph(dis_text)
    reach = reachable(graph)
    # The journal backend is reached through a function-pointer table (NlsBackend), so the
    # no-overwrite rename is checked as a direct edge of the backend's move function instead.
    if "FSUSER_RenameFile" not in graph.get("fs_move", ()):
        errors.append("FSUSER_RenameFile not called by backend fs_move")
    for sym in REQUIRED_REACHABLE:
        if sym not in reach:
            errors.append(f"{sym} unreachable from main")
    if mapfile is not None:
        text = open(mapfile, errors="replace").read()
        for lib in ("libctru.a(", "libcitro2d.a(", "libcitro3d.a("):
            if lib not in text:
                errors.append(f"linker map lacks {lib}")
    return errors


def u16z(b):
    return b.decode("utf-16-le").rstrip("\0")


def check_smdh(path):
    errors = []
    b = open(path, "rb").read()
    if len(b) != 0x36C0 or b[:4] != b"SMDH":
        return ["bad SMDH size/magic"], b
    if u16z(b[8:8 + 0x80]) != "FoldQuest":
        errors.append("SMDH short title")
    if u16z(b[8 + 0x80:8 + 0x180]) != "Fold the room by closing the lid":
        errors.append("SMDH long title")
    if u16z(b[8 + 0x180:8 + 0x200]) != "lafiabrew":
        errors.append("SMDH publisher")
    if b[0x2008:0x2018] != b"\0" * 16:
        errors.append("SMDH age ratings not cleared")
    if struct.unpack_from("<I", b, 0x2018)[0] != 0x7FFFFFFF:
        errors.append("SMDH region mask")
    if struct.unpack_from("<I", b, 0x2028)[0] != 0x105:
        errors.append("SMDH flags")
    # Pin the original procedural icon, independently of mutable SMDH metadata.
    if hashlib.sha256(b[0x2040:]).hexdigest() != "2aa0fee91a1c1fb30b2a05b7c642f86d6f8c0f15a263dd9a57c277007a3720a8":
        errors.append("SMDH procedural icon changed")
    return errors, b


def check_3dsx(path, smdh):
    b = open(path, "rb").read()
    if b[:4] != b"3DSX":
        return ["bad 3DSX magic"]
    hdr, = struct.unpack_from("<H", b, 4)
    if hdr < 0x20 + 12:
        return ["3DSX lacks extended header"]
    off, size = struct.unpack_from("<II", b, 0x20)
    return [] if b[off:off + size] == smdh else ["3DSX embedded SMDH differs"]


def align(n, a=0x40):
    return (n + a - 1) // a * a


def check_ncch(b, smdh):
    """Inspect the actual content, not a byte-string search or a sidecar CXI promise."""
    if len(b) < 0x200 or b[0x100:0x104] != b"NCCH":
        return ["bad NCCH size/magic"]
    errors = []
    if struct.unpack_from("<I", b, 0x104)[0] * 0x200 != len(b):
        errors.append("NCCH content size")
    for offset in (0x108, 0x118):  # partition/program identity
        if struct.unpack_from("<Q", b, offset)[0] != TITLE_ID:
            errors.append(f"NCCH title id at {offset:#x}")
    if b[0x150:0x160].rstrip(b"\0") != PRODUCT_CODE:
        errors.append("NCCH product code")
    # Existing unencrypted executable/application, Original3DS-compatible packaging.
    if b[0x188:0x190] != bytes.fromhex("0000000001020007"):
        errors.append("NCCH flags")
    if struct.unpack_from("<II", b, 0x198) != (0, 0):
        errors.append("NCCH logo region must be absent")
    if b[0x130:0x150] != bytes(32):
        errors.append("NCCH absent logo hash must be zero")
    off, size = struct.unpack_from("<II", b, 0x1A0)
    off, size = off * 0x200, size * 0x200
    if off < 0x200 or size < 0x200 or off + size > len(b):
        return errors + ["NCCH ExeFS bounds"]
    exefs = b[off:off + size]
    entries = {}
    for i in range(10):
        name, start, length = struct.unpack_from("<8sII", exefs, i * 16)
        name = name.rstrip(b"\0")
        if not name and start == length == 0:
            continue
        if name in (b"banner", b"logo"):
            errors.append(f"ExeFS {name.decode()} must be absent")
        if not name or name in entries or not length or 0x200 + start + length > size:
            errors.append("ExeFS entry name/size/bounds")
            continue
        payload = exefs[0x200 + start:0x200 + start + length]
        if hashlib.sha256(payload).digest() != exefs[0x1E0 - i * 32:0x200 - i * 32]:
            errors.append("ExeFS entry hash")
        entries[name] = payload
    if set(entries) != {b".code", b"icon"}:
        errors.append("ExeFS must contain only .code and icon")
    if entries.get(b"icon") != smdh:
        errors.append("ExeFS icon differs from verified SMDH")
    return errors


def check_cia(path, smdh):
    b = open(path, "rb").read()
    if len(b) < 0x2020:
        return ["truncated CIA header"]
    hdr, ctype, ver, cert, tik, tmd, meta = struct.unpack_from("<IHHIIII", b, 0)
    if hdr != 0x2020 or ctype != 0 or ver != 0:
        return ["bad CIA header size/type/version"]
    tmd_off = align(hdr) + align(cert) + align(tik)
    content_off = tmd_off + align(tmd)
    content_size, = struct.unpack_from("<Q", b, 0x18)
    if tmd < 0xB34 or content_off + content_size > len(b):
        return ["CIA TMD/content bounds"]
    sig, = struct.unpack_from(">I", b, tmd_off)
    if sig != 0x00010004:
        return [f"unexpected TMD signature type {sig:#x}"]
    errors = []
    tid, = struct.unpack_from(">Q", b, tmd_off + 0x18C)
    if tid != TITLE_ID:
        errors.append(f"CIA title id {tid:#018x} != {TITLE_ID:#018x}")
    count, = struct.unpack_from(">H", b, tmd_off + 0x1DE)
    index, flags, length = struct.unpack_from(">HHQ", b, tmd_off + 0xB08)
    if count != 1 or index != 0 or flags != 0 or length != content_size or b[0x20] != 0x80 or any(b[0x21:0x2020]):
        errors.append("CIA single unencrypted content flags/index/size")
    content = b[content_off:content_off + content_size]
    if hashlib.sha256(content).digest() != b[tmd_off + 0xB14:tmd_off + 0xB34]:
        errors.append("CIA TMD content hash")
    return errors + check_ncch(content, smdh)


def check_cxi(path, smdh):
    return check_ncch(open(path, "rb").read(), smdh)


def verify(build, elf, dis, mapfile):
    errors = []
    dis_text = open(dis, errors="replace").read()
    errors += check_elf(elf, dis_text, mapfile)
    return errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", required=True)
    ap.add_argument("--negative-control", action="store_true")
    args = ap.parse_args()
    b = args.build
    path = lambda n: os.path.join(b, n)
    if not args.negative_control:
        for n in (f"{TARGET}.elf", f"{TARGET}.dis", f"{TARGET}.map", f"{TARGET}.smdh", f"{TARGET}.3dsx",
                  f"{TARGET}.cia", f"{TARGET}.cxi"):
            if not os.path.isfile(path(n)) or os.path.getsize(path(n)) == 0:
                sys.exit(f"FAIL: missing or empty artifact {path(n)}")
        errors = verify(b, path(f"{TARGET}.elf"), path(f"{TARGET}.dis"), path(f"{TARGET}.map"))
        e, smdh = check_smdh(path(f"{TARGET}.smdh"))
        errors += e + check_3dsx(path(f"{TARGET}.3dsx"), smdh) + check_cia(path(f"{TARGET}.cia"), smdh) + check_cxi(path(f"{TARGET}.cxi"), smdh)
        if errors:
            sys.exit("FAIL: " + "; ".join(errors))
        print(f"PASS: {TARGET} ARM ELF with reachable game/APT/FS call path, SMDH, 3DSX, CIA title id {TITLE_ID:#018x}, CXI product code {PRODUCT_CODE.decode()}; CIA/CXI ExeFS only .code/icon, no banner/logo or NCCH logo region")
        return
    # Negative control 1: same-architecture stub launcher.
    stub = os.path.join(b, "negative")
    errors = verify(b, os.path.join(stub, "stub.elf"), os.path.join(stub, "stub.dis"), None)
    if any(e.startswith("wrong machine") or "not a 32-bit" in e for e in errors):
        sys.exit("FAIL: negative control was rejected for the wrong reason (machine): " + "; ".join(errors))
    if not any("unreachable from main" in e for e in errors) or "missing defined symbol fq_app_frame" not in errors:
        sys.exit("FAIL: verifier did NOT reject the same-ARM stub launcher for the missing game path: " + "; ".join(errors))
    # Negative control 2: real ELF but main() no longer calls the frame function.
    text = open(path(f"{TARGET}.dis"), errors="replace").read()
    mutated = re.sub(r"(bl\s+[0-9a-f]+ )<fq_app_frame>", r"\1<__aeabi_idiv0>", text)
    if mutated == text:
        sys.exit("FAIL: mutant did not change anything; control is vacuous")
    merrors = check_elf(path(f"{TARGET}.elf"), mutated, path(f"{TARGET}.map"))
    if "fq_app_frame unreachable from main" not in merrors:
        sys.exit("FAIL: verifier accepted the mutant with the frame call removed")
    print(f"PASS: verifier rejected same-ARM stub ({len(errors)} findings) and call-edge mutant")
    # Mutate the freshly built production CIA, not a surrogate header-only artifact.
    # Assert specific structural errors, so a generic TMD hash failure cannot earn credit.
    cia = bytearray(open(path(f"{TARGET}.cia"), "rb").read())
    hdr, _, _, cert, tik, tmd, _ = struct.unpack_from("<IHHIIII", cia, 0)
    tmd_off = align(hdr) + align(cert) + align(tik)
    content_off = tmd_off + align(tmd)
    exefs_off = content_off + struct.unpack_from("<I", cia, content_off + 0x1A0)[0] * 0x200
    _, smdh = check_smdh(path(f"{TARGET}.smdh"))
    controls = [(content_off + 0x198, struct.pack("<II", 1, 1), "NCCH logo region must be absent"),
                (exefs_off, b"banner\0\0", "ExeFS banner must be absent"),
                (exefs_off, b"logo\0\0\0\0", "ExeFS logo must be absent"),
                (tmd_off + 0xB0A, b"\0\1", "CIA single unencrypted content flags/index/size"),
                (content_off + 0x150, b"BAD", "NCCH product code")]
    with tempfile.TemporaryDirectory(dir=stub) as tmp:
        mutant = os.path.join(tmp, "packaging.cia")
        for offset, replacement, expected in controls:
            data = bytearray(cia)
            data[offset:offset + len(replacement)] = replacement
            with open(mutant, "wb") as out:
                out.write(data)
            if expected not in check_cia(mutant, smdh):
                sys.exit("FAIL: CIA packaging control missed: " + expected)
    print("PASS: actual CIA mutants rejected for NCCH logo region, ExeFS banner/logo, content flags and product identity")


if __name__ == "__main__":
    main()
