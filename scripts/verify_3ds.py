#!/usr/bin/env python3
"""Verify FoldQuest native artifacts beyond magic bytes.

Checks: ARM ELF + defined application/SDK symbols, call-graph reachability from main() (parsed from
the ELF disassembly), libctru/citro2d link evidence in the linker map, SMDH text/flags/age/region,
3DSX-embedded SMDH, CIA TMD title id and CXI (NCCH) title id + product code.
--negative-control: run the same checks on a same-architecture stub launcher (compiled and linked
for ARM with libctru, no game code) and on a mutated copy of the real disassembly; both MUST be
rejected for the missing game path, NOT for a wrong machine type."""
import argparse
import os
import re
import struct
import sys

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


def check_cia(path):
    b = open(path, "rb").read()
    hdr, ctype, ver, cert, tik, tmd, meta = struct.unpack_from("<IHHIIII", b, 0)
    if hdr != 0x2020:
        return ["bad CIA header size"]
    tmd_off = align(hdr) + align(cert) + align(tik)
    sig, = struct.unpack_from(">I", b, tmd_off)
    if sig != 0x00010004:
        return [f"unexpected TMD signature type {sig:#x}"]
    tid, = struct.unpack_from(">Q", b, tmd_off + 0x18C)
    return [] if tid == TITLE_ID else [f"CIA title id {tid:#018x} != {TITLE_ID:#018x}"]


def check_cxi(path):
    b = open(path, "rb").read()
    if b[0x100:0x104] != b"NCCH":
        return ["bad NCCH magic"]
    errors = []
    tid, = struct.unpack_from("<Q", b, 0x108)
    if tid != TITLE_ID:
        errors.append(f"NCCH title id {tid:#018x}")
    if b[0x150:0x160].rstrip(b"\0") != PRODUCT_CODE:
        errors.append("NCCH product code")
    return errors


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
        errors += e + check_3dsx(path(f"{TARGET}.3dsx"), smdh) + check_cia(path(f"{TARGET}.cia")) + check_cxi(path(f"{TARGET}.cxi"))
        if errors:
            sys.exit("FAIL: " + "; ".join(errors))
        print(f"PASS: {TARGET} ARM ELF with reachable game/APT/FS call path, SMDH, 3DSX, CIA title id {TITLE_ID:#018x}, CXI product code {PRODUCT_CODE.decode()}")
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


if __name__ == "__main__":
    main()
