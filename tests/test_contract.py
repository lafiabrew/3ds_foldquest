"""FoldQuest contract tests. They drive the REAL host build of the production code
(build/host/3ds_foldquest_host: scripted input + simulated lifecycle events, labeled HOST SIM)
and parse the diagnostic log it actually writes. Nothing here is hardware evidence."""
import json
import os
import pathlib
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
