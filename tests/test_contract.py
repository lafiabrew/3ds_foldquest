"""FoldQuest contract tests. They drive the REAL host build of the production code
(build/host/3ds_foldquest_host: scripted input + simulated lifecycle events, labeled HOST SIM)
and parse the diagnostic log it actually writes. Nothing here is hardware evidence."""
import hashlib
import json
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
HOST = ROOT / "build" / "host" / "3ds_foldquest_host"
REQUIRED = ROOT / "specs" / "required-tests.json"
FIELDS = {"schema", "app", "build", "event", "elapsed_us", "seed", "settings", "capabilities"}
sys.path.insert(0, str(ROOT / "scripts"))


def setUpModule():
    subprocess.run(["make", "host"], cwd=ROOT, check=True, capture_output=True)
    (ROOT / "build").mkdir(exist_ok=True)


def run_host(root, script, check=True):
    proc = subprocess.run([str(HOST), "--sd-root", str(root), "--script", script], cwd=ROOT,
                          capture_output=True, text=True, timeout=30)
    if check:
        assert proc.returncode == 0, proc.stdout + proc.stderr
    return proc


def log_path(root):
    return pathlib.Path(root) / "3ds" / "logs" / "3ds_foldquest.log"


def parse_log(path):
    raw = pathlib.Path(path).read_bytes()
    assert len(raw) <= 256 * 1024
    raw.decode("ascii")
    lines = raw.split(b"\n")
    assert lines[-1] == b""
    records = [json.loads(line) for line in lines[:-1]]
    assert len(lines[0]) <= 199
    return records


SOLVE = "right right right sleep wake right right right sleep wake right right right right"


class ContractTests(unittest.TestCase):
    def test_suspend_resume_solve_reset(self):
        """The room is solved only through two simulated lifecycle resumes; asleep frames change nothing."""
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as tmp:
            # blocked at the closed gate without a fold
            p = run_host(tmp, "right right right right right")
            self.assertIn("moves=2", p.stdout)
            self.assertIn("solved=0", p.stdout)
            self.assertIn("HOST SIM (not hardware)", p.stdout)
            # frames delivered while asleep are rejected and counted; then the wake folds the room once
            p = run_host(tmp, "right right sleep right right left wake right")
            self.assertIn("moves=3", p.stdout)
            self.assertIn("rejected_asleep=3", p.stdout)
            self.assertIn("resumes=1", p.stdout)
            # complete solve, export diagnostics, reset, and quit
            p = run_host(tmp, SOLVE + " export reset quit")
            self.assertIn("solved=0", p.stdout)   # reset after solve
            self.assertIn("resets=1", p.stdout)
            self.assertIn("resumes=2", p.stdout)
            recs = parse_log(log_path(tmp))
            self.assertEqual([r["event"] for r in recs], ["log_open", "export"])
            s = recs[1]["settings"]
            self.assertEqual((s["solved"], s["resumes"], s["moves"], s["phase"]), (True, 2, 8, 0))
            # a spurious wake never flips the room; solved state ends the session visibly
            p = run_host(tmp, "wake wake")
            self.assertIn("resumes=0", p.stdout)
            p = run_host(tmp, SOLVE)
            self.assertIn("solved=1", p.stdout)
            # touch: far tile refused, adjacent accepted (grid origin 28,28; cell 24)
            p = run_host(tmp, "tap:100,100 tap:81,57")
            self.assertIn("moves=1", p.stdout)
            # HOME suspend counts as a real suspend/resume as well
            p = run_host(tmp, "right right right home wake right right")
            self.assertIn("resumes=1", p.stdout)
            self.assertIn("moves=4", p.stdout)

    def test_log_schema_and_recovery(self):
        """Exported log follows the 8-field schema; corrupt/unwritable states are visible and retained."""
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as tmp:
            run_host(tmp, "right export")
            run_host(tmp, "right export")
            recs = parse_log(log_path(tmp))
            self.assertEqual(len(recs), 3)
            for r in recs:
                self.assertEqual(set(r), FIELDS)
                self.assertEqual((r["schema"], r["app"], r["seed"]), (1, "3ds_foldquest", 1))
                self.assertIsInstance(r["elapsed_us"], int)
            self.assertEqual(recs[0]["event"], "log_open")
            self.assertEqual(recs[0]["settings"], {})
            # host has no 3DS model/touch/APT: reported as null/false, never invented
            self.assertIsNone(recs[1]["capabilities"]["model"])
            self.assertFalse(recs[1]["capabilities"]["apt_hooks"])
            self.assertFalse(recs[1]["capabilities"]["new3ds_enhancements"])
            self.assertEqual(recs[1]["elapsed_us"], 2 * 16667)  # two awake frames, simulated clock
            # a torn tail is never trimmed: export refuses, bytes identical, status visible
            path = log_path(tmp)
            with open(path, "ab") as f:
                f.write(b'{"schema":1,"app":"3ds_fo')
            torn = path.read_bytes()
            p = run_host(tmp, "export")
            self.assertIn("EXPORT FAILED: unknown/corrupt log kept", p.stdout)
            self.assertEqual(path.read_bytes(), torn)
            # operator preserves evidence and removes it; a valid retry then succeeds
            path.rename(path.with_name("evidence.copy"))
            p = run_host(tmp, "export")
            self.assertIn("EXPORTED", p.stdout)
            self.assertEqual(len(parse_log(path)), 2)
            self.assertEqual(path.with_name("evidence.copy").read_bytes(), torn)
            # unwritable storage (a regular file occupies the 3ds/ directory name): visible failure, no
            # log bytes created; works on any privilege level, unlike a chmod-based case
            with tempfile.TemporaryDirectory(dir=ROOT / "build") as blocked:
                (pathlib.Path(blocked) / "3ds").write_bytes(b"not a directory")
                p = run_host(blocked, "right export")
                self.assertIn("EXPORT FAILED", p.stdout)
                self.assertEqual(sorted(os.listdir(blocked)), ["3ds"])
                self.assertEqual((pathlib.Path(blocked) / "3ds").read_bytes(), b"not a directory")
            p = run_host(tmp, "export")
            self.assertIn("EXPORTED", p.stdout)
            # bad CLI is a visible failure, not a silent default
            bad = subprocess.run([str(HOST), "--bogus"], cwd=ROOT, capture_output=True, text=True)
            self.assertNotEqual(bad.returncode, 0)

    def test_required_names(self):
        """Every name in specs/required-tests.json is a discoverable, non-skipped test of this module."""
        spec = json.loads(REQUIRED.read_text())
        loader = unittest.TestLoader()
        names = set(loader.getTestCaseNames(ContractTests))
        self.assertEqual(spec["contract_file"], "tests/test_contract.py")
        for name in spec["tests"]:
            self.assertIn(name, names)
        self.assertEqual(spec["extra_engine_verifier"], False)

    def test_native_unit_binaries(self):
        """Run the ASan/UBSan core/app/log suite and the real APT-hook adapter suite as part of the contract."""
        for name in ("test_fq", "test_apt"):
            p = subprocess.run([str(ROOT / "build" / "host" / name)], cwd=ROOT, capture_output=True, text=True, timeout=120)
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
            self.assertIn("PASS " + name, p.stdout)

    def test_packaging_source_and_original_icon(self):
        """Only the original icon is generated; no dormant embedded banner or import flags survive."""
        self.assertFalse((ROOT / "meta" / "generate_bnr_shared.py").exists())
        for path in (ROOT / "meta").glob("*.py"):
            text = path.read_text()
            self.assertNotIn("CBMD_PREFIX_ZLIB_B64", text)
            self.assertNotIn("BANNER_CGFX_HEADER", text)
            self.assertNotIn("base64", text)
        makefile = (ROOT / "Makefile").read_text()
        for flag in ("-exefslogo", "-logo", "-banner"):
            self.assertNotIn(flag, makefile)
        self.assertRegex((ROOT / "meta/app.rsf").read_text(), r"Logo\s*:\s*None\b")
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as tmp:
            subprocess.run([sys.executable, "meta/generate_assets.py", tmp], cwd=ROOT, check=True, capture_output=True)
            self.assertEqual(os.listdir(tmp), ["icon.png"])
            self.assertEqual(hashlib.sha256((pathlib.Path(tmp) / "icon.png").read_bytes()).hexdigest(),
                             "305c722c42d4cc66df4dbfb96d02526e9f2bbcbdc5a402dec485602293172eab")
            # Moving the metadata patch out of the removed encoder must not alter icon bytes.
            smdh = bytearray([0xA5] * 0x36C0)
            smdh[:4] = b"SMDH"
            expected = bytearray(smdh)
            expected[0x2008:0x2018] = bytes(16)
            struct.pack_into("<I", expected, 0x2018, 0x7FFFFFFF)
            struct.pack_into("<I", expected, 0x2028, 0x105)
            path = pathlib.Path(tmp) / "fixture.smdh"
            path.write_bytes(smdh)
            subprocess.run([sys.executable, "meta/generate_assets.py", "--patch-smdh", str(path)],
                           cwd=ROOT, check=True, capture_output=True)
            self.assertEqual(path.read_bytes(), expected)
            path.write_bytes(b"not SMDH")
            result = subprocess.run([sys.executable, "meta/generate_assets.py", "--patch-smdh", str(path)],
                                    cwd=ROOT, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(path.read_bytes(), b"not SMDH")

    def test_packaging_ncch_absence_regressions(self):
        """Parser fixtures prove optional assets are rejected even with zero-length ExeFS entries."""
        import verify_3ds as v
        icon = b"SMDH parser fixture, not a launchable application"

        def ncch(extra=None):
            b = bytearray(0x1000)
            b[0x100:0x104] = b"NCCH"
            struct.pack_into("<I", b, 0x104, len(b) // 0x200)
            for off in (0x108, 0x118):
                struct.pack_into("<Q", b, off, v.TITLE_ID)
            b[0x150:0x150 + len(v.PRODUCT_CODE)] = v.PRODUCT_CODE
            b[0x188:0x190] = bytes.fromhex("0000000001020007")
            struct.pack_into("<II", b, 0x1A0, 1, 7)
            entries = [(b".code", b"ARM parser fixture"), (b"icon", icon)]
            if extra is not None:
                entries.append(extra)
            start = 0
            for i, (name, payload) in enumerate(entries):
                struct.pack_into("<8sII", b, 0x200 + i * 16, name, start, len(payload))
                b[0x400 + start:0x400 + start + len(payload)] = payload
                b[0x200 + 0x1E0 - i * 32:0x200 + 0x200 - i * 32] = hashlib.sha256(payload).digest()
                start += v.align(len(payload), 0x200)
            return b

        clean = ncch()
        self.assertEqual(v.check_ncch(clean, icon), [])
        for name in (b"banner", b"logo"):
            for payload in (b"", b"unlicensed fixture"):
                with self.subTest(name=name, length=len(payload)):
                    self.assertIn(f"ExeFS {name.decode()} must be absent", v.check_ncch(ncch((name, payload)), icon))
        region = bytearray(clean)
        struct.pack_into("<II", region, 0x198, 4, 1)
        self.assertIn("NCCH logo region must be absent", v.check_ncch(region, icon))
        region = bytearray(clean)
        region[0x130] = 1
        self.assertIn("NCCH absent logo hash must be zero", v.check_ncch(region, icon))
        self.assertIn("ExeFS icon differs from verified SMDH", v.check_ncch(clean, b"other icon"))
        for off, expected in ((0x118, "NCCH title id at 0x118"), (0x150, "NCCH product code"),
                              (0x18F, "NCCH flags"), (0x400, "ExeFS entry hash")):
            mutated = bytearray(clean)
            mutated[off] ^= 1
            self.assertIn(expected, v.check_ncch(mutated, icon))
        struct.pack_into("<II", clean, 0x1A0, 100, 1)
        self.assertIn("NCCH ExeFS bounds", v.check_ncch(clean, icon))

    def test_verifier_logic_and_abi_flags(self):
        """The artifact verifier's graph logic rejects a missing edge; the build avoids ABI-breaking flags."""
        import verify_3ds as v
        dis = "00100000 <main>:\n  100004:\teb000001 \tbl\t100010 <a>\n00100010 <a>:\n  100014:\teafffff9 \tb\t100020 <c>\n00100020 <c>:\n"
        g = v.parse_graph(dis)
        self.assertEqual(v.reachable(g), {"main", "a", "c"})
        self.assertNotIn("c", v.reachable(v.parse_graph(dis.replace("<c>\n", "<d>\n", 1))))
        self.assertNotIn("a", v.reachable(v.parse_graph(dis.replace("bl\t100010 <a>", "nop"))))
        makefile = (ROOT / "Makefile").read_text()
        self.assertNotIn("-fno-short-enums", makefile)   # would break the SDK shared struct layout
        self.assertIn("-D__3DS__", makefile)
        rsf = (ROOT / "meta" / "app.rsf").read_text()
        self.assertIn("0x0D3F7", rsf)
        self.assertIn("CTR-P-FOLD", rsf)
        self.assertNotIn("CpuSpeed", rsf)                # no New3DS-only requirement in the baseline


if __name__ == "__main__":
    unittest.main()
