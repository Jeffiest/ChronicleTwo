"""Exercise real host patches across the in-tree/out-of-tree build boundary."""

import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

import upstream_host_fixups
import win_rewrite

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "ps2/src"
SCRIPT = Path(upstream_host_fixups.__file__).resolve()


def contents(directory):
    return {path.relative_to(directory).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in directory.rglob("*") if path.is_file()}


class HostMirrorTests(unittest.TestCase):
    def setUp(self):
        build = ROOT / "port/build"
        build.mkdir(parents=True, exist_ok=True)
        inside = tempfile.TemporaryDirectory(prefix="host-mirror-", dir=build)
        self.addCleanup(inside.cleanup)
        self.inside = inside.name
        outside = tempfile.TemporaryDirectory(prefix="host-mirror-")
        self.addCleanup(outside.cleanup)
        self.outside = outside.name

    def prepare(self, source, output, env=None):
        output = Path(output)
        return subprocess.run([sys.executable, str(SCRIPT), str(source), str(output / "src"),
                               str(output / "prepared.stamp")],
                              env=env, text=True, capture_output=True, timeout=30)

    def test_all_patches_and_windows_rules_inside_and_outside_checkout(self):
        original = contents(SOURCE)
        patched = re.findall(r"^\+\+\+ b/ps2/src/(.+)$",
                             upstream_host_fixups.PATCH.read_text(), re.MULTILINE)
        expected = None
        for directory in (self.outside, self.inside):
            with self.subTest(directory=directory):
                result = self.prepare(SOURCE, directory)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertTrue((Path(directory) / "prepared.stamp").is_file())
                mirror = Path(directory) / "src"
                actual = contents(mirror)
                self.assertEqual(actual.keys(), original.keys())
                mainloop = (mirror / "mainloop.cpp").read_text(encoding="latin-1")
                for declaration in ("static int NextLoopNo;", "static int LoopNo;", "static CSaveData *ActiveSaveData;",
                                    "static void InitPadTable(int language)",
                                    "static void VSyncCallBack(int unused)"):
                    self.assertNotIn(declaration, mainloop)
                for filename in patched:
                    self.assertNotEqual(actual[filename], original[filename], filename)
                if expected is not None:
                    self.assertEqual(actual, expected)
                expected = actual
                for key in win_rewrite.RULES:
                    if key.startswith("src/"):
                        win_rewrite.check(key, win_rewrite.read(mirror / key[4:]))
        self.assertEqual(contents(SOURCE), original)

    def test_inherited_repository_environment_does_not_redirect_patch(self):
        env = os.environ.copy()
        env.update(GIT_DIR=str(ROOT / ".git"), GIT_WORK_TREE=str(ROOT),
                   GIT_COMMON_DIR=str(ROOT / ".git"), GIT_INDEX_FILE=str(ROOT / ".git/index"),
                   GIT_PREFIX="port/build/")
        result = self.prepare(SOURCE, self.inside, env)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_source_drift_fails_before_patch_or_stamp(self):
        source = Path(self.outside) / "source"
        shutil.copytree(SOURCE, source)
        water = source / "water.cpp"
        data = water.read_bytes()
        changed, count = re.subn(rb"\buptr\s+base;", b"uptr changed_base;", data)
        self.assertEqual(count, 1)
        water.write_bytes(changed)
        result = self.prepare(source, self.inside)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("patch does not match source mirror", result.stderr)
        self.assertFalse((Path(self.inside) / "prepared.stamp").exists())
        self.assertEqual(contents(Path(self.inside) / "src"), contents(source))


if __name__ == "__main__":
    unittest.main()
