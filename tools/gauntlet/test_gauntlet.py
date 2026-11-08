"""Tests of the gauntlet runner itself: python3 -I tools/gauntlet/test_gauntlet.py

Covers the report schema and exit code, baseline diffing, stage selection and gate order, the
JUnit reader, and the screenshot sanity and tolerance compare (pure PNG reader and PIL alike).
"""
import json
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from gauntlet import baseline, ctest, images, report, stages, util  # noqa: E402


class ReportTest(unittest.TestCase):
    def test_schema_and_exit_code(self):
        rep = report.new_report(["--gate", "G0"], {"gpu": False}, {"ChronicleTwo": {"path": "/x", "present": True, "git": True,
                                                                                 "head": "abc", "branch": "b", "dirty": False}})
        rep["stages"].append(report.stage("env", "G0", "pass", "ok"))
        rep["stages"].append(report.stage("web-node", "G3", "not_run", "absent"))
        report.finish(rep)
        self.assertEqual(report.validate(rep), [])
        self.assertEqual(rep["exit_code"], 0)
        self.assertEqual(rep["verdict"], "pass")
        rep["stages"].append(report.stage("ctest", "G0", "fail", "2 new", baseline={"new": ["A.B"], "fixed": [], "known": []}))
        report.finish(rep)
        self.assertEqual(rep["exit_code"], 1)
        self.assertEqual(report.validate(rep), [])
        rep["stages"].append(report.stage("coop", "G4", "error", "boom"))
        self.assertEqual(report.finish(rep)["exit_code"], 1)

    def test_not_run_alone_is_a_pass(self):
        rep = report.new_report([], {}, {})
        rep["stages"].append(report.stage("retail-title", "G5", "not_run", "no disc"))
        self.assertEqual(report.finish(rep)["exit_code"], 0)

    def test_bad_status_is_refused(self):
        with self.assertRaises(ValueError):
            report.stage("x", "G0", "skipped", "")

    def test_write_json_md_and_latest(self):
        with tempfile.TemporaryDirectory() as tmp:
            rep = report.new_report([], {}, {"MoonMod": {"path": "/m", "present": False}})
            rep["stages"].append(report.stage("ctest", "G0", "fail", "x", 1.5, "ctest -LE gpu", "/log", ["/log"],
                                              {"new": ["New.Case"], "fixed": ["Old.Case"], "known": []}))
            directory = os.path.join(tmp, "20260101-000000")
            path = report.write(rep, directory)
            with open(path) as f:
                loaded = json.load(f)
            self.assertEqual(report.validate(loaded), [])
            with open(os.path.join(directory, "report.md")) as f:
                md = f.read()
            self.assertIn("New.Case", md)
            self.assertIn("Old.Case", md)
            self.assertIn("ctest -LE gpu", md)
            self.assertEqual(os.readlink(os.path.join(tmp, "latest")), "20260101-000000")

    def test_exit_code_helper_does_not_mutate(self):
        rep = report.new_report([], {}, {})
        rep["stages"].append(report.stage("a", "G0", "fail", ""))
        self.assertEqual(report.exit_code(rep), 1)


class BaselineTest(unittest.TestCase):
    def test_diff_new_known_fixed(self):
        data = {"ctest-cpu": {"failures": ["Known.One", "Fixed.Now", "Unseen.Case"]}}
        d = baseline.diff(["Known.One", "Brand.New"], ["Known.One", "Fixed.Now", "Passing.Case"], data, "ctest-cpu")
        self.assertEqual(d["new"], ["Brand.New"])
        self.assertEqual(d["known"], ["Known.One"])
        self.assertEqual(d["fixed"], ["Fixed.Now"])
        self.assertEqual(d["unseen"], ["Unseen.Case"])
        self.assertTrue(d["has_baseline"])

    def test_no_baseline(self):
        d = baseline.diff(["A"], ["A"], {}, "ctest-cpu")
        self.assertFalse(d["has_baseline"])
        self.assertEqual(d["new"], ["A"])

    def test_update_writes_sorted_unique(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "b.json")
            baseline.update(path, "ctest-cpu", ["B", "A", "B"], {"total": 3}, head="h")
            data = baseline.load(path)
            self.assertEqual(data["ctest-cpu"]["failures"], ["A", "B"])
            self.assertEqual(data["ctest-cpu"]["head"], "h")
            self.assertEqual(data["platform"], baseline.platform_name())
            baseline.update(path, "ctest-gpu", [], {"total": 1})
            self.assertIn("ctest-cpu", baseline.load(path))


class SelectionTest(unittest.TestCase):
    def test_gates_are_in_order_and_complete(self):
        gates = [gate for _, gate, _ in stages.STAGES]
        self.assertEqual(gates, sorted(gates))
        self.assertEqual(sorted(set(gates)), stages.GATES)

    def test_select_by_gate_and_stage(self):
        self.assertEqual([n for n, _, _ in stages.select(["G0"])], ["env", "build", "ctest"])
        self.assertEqual([n for n, _, _ in stages.select(None, ["coop", "env"])], ["env", "coop"])
        self.assertEqual(len(stages.select()), len(stages.STAGES))
        self.assertEqual([g for _, g, _ in stages.select(["G1", "G3"])], ["G1", "G1", "G1", "G3", "G3"])
        self.assertEqual([n for n, _, _ in stages.select(["G1"])], ["capacity", "toolkit", "toolkit-limits"])

    def test_stage_names_are_unique(self):
        names = [n for n, _, _ in stages.STAGES]
        self.assertEqual(len(names), len(set(names)))

    def test_run_list_and_unknown_stage(self):
        import run
        self.assertEqual(run.main(["--list"]), 0)
        self.assertEqual(run.main(["--stage", "nonsense"]), 2)

    def test_red_gate_blocks_the_next(self):
        import run
        saved = list(stages.STAGES)
        calls = []

        def red(ctx):
            calls.append("red")
            return report.stage("red", "G0", "fail", "x")

        def later(ctx):
            calls.append("later")
            return report.stage("later", "G1", "pass", "y")
        stages.STAGES[:] = [("red", "G0", red), ("later", "G1", later)]
        try:
            with tempfile.TemporaryDirectory() as tmp:
                code = run.main(["--stage", "red", "--stage", "later", "--report-dir", tmp])
                with open(os.path.join(tmp, "report.json")) as f:
                    rep = json.load(f)
                self.assertEqual(code, 1)
                self.assertEqual(calls, ["red"])
                self.assertEqual([s["status"] for s in rep["stages"]], ["fail", "not_run"])
                calls.clear()
                code = run.main(["--stage", "red", "--stage", "later", "--keep-going", "--report-dir", tmp])
                self.assertEqual(calls, ["red", "later"])
        finally:
            stages.STAGES[:] = saved


class MarkersTest(unittest.TestCase):
    def test_own_paths_do_not_make_identical_runs_differ(self):
        prefixes = ["dataread: indexed", "mglib: frame"]
        with tempfile.TemporaryDirectory() as tmp:
            logs = []
            for tag in ("a", "b"):
                log = os.path.join(tmp, "run-%s.log" % tag)
                with open(log, "w", encoding="utf-8") as f:
                    f.write("noise\n[t] dataread: indexed 86597 files under /data.zip\n"
                            "mglib: frame 120 written to %s/run-%s.png\nsaved to %s/run-%s-save\n" % (tmp, tag, tmp, tag))
                logs.append(stages._markers(log, prefixes, {"screenshot": "%s/run-%s.png" % (tmp, tag),
                                                              "save": "%s/run-%s-save" % (tmp, tag)}))
            self.assertEqual(logs[0], logs[1])
            self.assertEqual(logs[0], ["dataread: indexed 86597 files under /data.zip", "mglib: frame 120 written to <screenshot>"])
            self.assertNotEqual(stages._markers(os.path.join(tmp, "run-a.log"), prefixes),
                                stages._markers(os.path.join(tmp, "run-b.log"), prefixes))

    def test_a_real_difference_still_differs(self):
        with tempfile.TemporaryDirectory() as tmp:
            found = []
            for tag, frames in (("a", 120), ("b", 119)):
                log = os.path.join(tmp, "%s.log" % tag)
                with open(log, "w", encoding="utf-8") as f:
                    f.write("mglib: frame %d written to %s/%s.png\n" % (frames, tmp, tag))
                found.append(stages._markers(log, ["mglib: frame"], {"screenshot": "%s/%s.png" % (tmp, tag)}))
            self.assertNotEqual(found[0], found[1])


class ToolkitAndBuildTest(unittest.TestCase):
    def _ctx(self, tmp, **options):
        config = util.read_json(os.path.join(HERE, "stages.json"))
        base = {"clean": False, "no_build": False, "gpu": False, "update_baseline": False, "jobs": 1, "timeout": 60,
                "build_timeout": 60, "build_targets": [], "iso": None, "min_free_gb": 0, "coop_build": None, "web_build": None,
                "data": None, "approve_golden": False}
        base.update(options)
        return {"root": tmp, "gauntlet_dir": HERE, "env_sh": os.path.join(HERE, "env.sh"), "env": {}, "build_dir": os.path.join(tmp, "build"),
                "report_dir": os.path.join(tmp, "report"), "cache_dir": os.path.join(tmp, "cache"), "golden_dir": os.path.join(tmp, "golden"),
                "baseline_path": os.path.join(tmp, "b.json"), "config": config, "repos": {"DC2-Toolkit": {"path": "/nowhere", "present": False}},
                "options": base}

    def test_fold(self):
        s = lambda status: report.stage("x", "G0", status, "")  # noqa: E731
        self.assertEqual(stages.fold([s("pass"), s("not_run")]), "pass")
        self.assertEqual(stages.fold([s("pass"), s("fail"), s("error")]), "fail")
        self.assertEqual(stages.fold([s("pass"), s("error")]), "error")
        self.assertEqual(stages.fold([s("not_run"), s("not_run")]), "not_run")
        self.assertEqual(stages.fold([]), "not_run")

    def test_toolkit_absent_is_not_run(self):
        with tempfile.TemporaryDirectory() as tmp:
            for func in (stages.stage_toolkit, stages.stage_toolkit_limits):
                result = func(self._ctx(tmp))
                self.assertEqual(result["status"], "not_run")
                self.assertIn("not beside", result["summary"])
            ctx = self._ctx(tmp)
            ctx["repos"]["DC2-Toolkit"] = {"path": tmp, "present": True}
            self.assertEqual(stages.stage_toolkit(ctx)["status"], "not_run")

    def test_build_passes_only_the_targets_to_ct_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            ctx = self._ctx(tmp, build_targets=["darkchronicle"])
            fake = os.path.join(tmp, "env.sh")
            seen = os.path.join(tmp, "args.txt")
            with open(fake, "w") as f:
                f.write('ct_build() { printf "%%s\\n" "$@" > "%s"; }\n' % seen)
            ctx["env_sh"] = fake
            os.makedirs(ctx["build_dir"])
            with open(os.path.join(ctx["build_dir"], "CMakeCache.txt"), "w") as f:
                f.write("")
            result = stages.stage_build(ctx)
            self.assertEqual(result["status"], "pass")
            with open(seen) as f:
                self.assertEqual(f.read().split(), ["darkchronicle"])

    def test_no_build_reports_the_binary_age(self):
        with tempfile.TemporaryDirectory() as tmp:
            ctx = self._ctx(tmp, no_build=True)
            self.assertEqual(stages.stage_build(ctx)["status"], "error")
            os.makedirs(ctx["build_dir"])
            binary = os.path.join(ctx["build_dir"], "darkchronicle_tests")
            with open(binary, "w") as f:
                f.write("x")
            os.utime(binary, (1_700_000_000, 1_700_000_000))
            os.makedirs(os.path.join(tmp, "port", "src"))
            with open(os.path.join(tmp, "port", "src", "a.cpp"), "w") as f:
                f.write("//")
            result = stages.stage_build(ctx)
            self.assertEqual(result["status"], "not_run")
            self.assertIn("stale build", result["summary"])
            self.assertEqual(result["details"]["tests"]["newer_sources"], 1)
            self.assertEqual(result["details"]["tests"]["newer_sample"], ["port/src/a.cpp"])
            self.assertTrue(result["details"]["tests"]["linked"].startswith("2023-11-14"))


    def test_every_stage_degrades_without_a_crash(self):
        """With nothing to check (no build, no toolkit, no MoonMod, no web build, no relay, no disc) every stage
        returns not_run or error with a reason; none raises."""
        saved = os.environ.pop("DC2_DATA", None), os.environ.pop("DC2_ISO", None)
        try:
            with tempfile.TemporaryDirectory() as tmp:
                ctx = self._ctx(tmp, no_build=True)
                ctx["env"] = util.read_env_sh(os.path.join(HERE, "env.sh"))
                ctx["env"]["CT_LOCK_DIR"] = ""
                ctx["repos"] = {n: {"path": os.path.join(tmp, n), "present": False} for n in ("DC2-Toolkit", "MoonMod", "DC_Web")}
                for name, gate, func in stages.STAGES:
                    result = func(ctx)
                    rep = report.new_report([], {}, {})
                    rep["stages"].append(result)
                    self.assertEqual(report.validate(report.finish(rep)), [], name)
                    if name == "env":
                        continue
                    self.assertIn(result["status"], ("not_run", "error"), name)
                    self.assertTrue(result["summary"], name)
                    if gate in ("G1", "G2", "G3", "G4", "G5") and name not in ("capacity", "content"):
                        self.assertEqual(result["status"], "not_run", "%s: %s" % (name, result["summary"]))
        finally:
            for key, value in zip(("DC2_DATA", "DC2_ISO"), saved):
                if value is not None:
                    os.environ[key] = value

    def test_coop_build_absent_is_not_run(self):
        with tempfile.TemporaryDirectory() as tmp:
            relay = os.path.join(tmp, "coop", "relay")
            os.makedirs(os.path.join(relay, "src"))
            open(os.path.join(relay, "server.mjs"), "w").close()
            ctx = self._ctx(tmp, coop_build=os.path.join(tmp, "nowhere"))
            result = stages.stage_coop(ctx)
            self.assertEqual(result["status"], "not_run")
            self.assertIn("no co-op build directory", result["summary"])

    def test_source_root_from_cmake_cache(self):
        with tempfile.TemporaryDirectory() as tmp:
            with open(os.path.join(tmp, "CMakeCache.txt"), "w") as f:
                f.write("FOO:BOOL=ON\nCMAKE_HOME_DIRECTORY:INTERNAL=/src/tree\n")
            self.assertEqual(stages._source_root(tmp), "/src/tree")
            self.assertIsNone(stages._source_root(os.path.join(tmp, "none")))

    def test_disk_guard(self):
        with tempfile.TemporaryDirectory() as tmp:
            ctx = self._ctx(tmp, min_free_gb=0)
            self.assertIsNone(stages.disk_guard(ctx, "build", "G0"))
            ctx["options"]["min_free_gb"] = 1e9
            result = stages.disk_guard(ctx, "build", "G0")
            self.assertEqual(result["status"], "error")
            self.assertIn("GB free", result["summary"])
            ctx["options"]["min_free_gb"] = 1e9
            self.assertEqual(stages.stage_build(ctx)["status"], "error")
            ctx["options"]["min_free_gb_tests"] = 0
            self.assertIsNone(stages.disk_guard(ctx, "ctest", "G0", tests=True))
            ctx["options"]["min_free_gb_tests"] = 1e9
            self.assertIn("--min-free-gb-tests", stages.disk_guard(ctx, "ctest", "G0", tests=True)["summary"])

    def test_contaminated_runs(self):
        info = lambda t: {"linked": t}  # noqa: E731
        self.assertIsNone(stages.contaminated({"output": "ok", "binary_before": info("a"), "binary_after": info("a")}))
        self.assertIn("could not find", stages.contaminated({"output": "Could not find executable x\n", "binary_before": None,
                                                             "binary_after": None}))
        self.assertIn("linked a at the start, b at the end", stages.contaminated({"output": "", "binary_before": info("a"),
                                                                                 "binary_after": info("b")}))
        self.assertIn("disk filled up", stages.contaminated({"output": "write: No space left on device\n"}))

    def test_reorder_last(self):
        chosen = stages.select()
        moved = stages.reorder(chosen, ["toolkit-limits", "toolkit"])
        self.assertEqual([n for n, _, _ in moved][-2:], ["toolkit-limits", "toolkit"])
        self.assertEqual(len(moved), len(chosen))
        self.assertEqual(stages.reorder(chosen, []), chosen)


class LockTest(unittest.TestCase):
    def test_lock_file_is_held_and_released(self):
        with tempfile.TemporaryDirectory() as tmp:
            ctx = {"env": {"CT_LOCK_DIR": tmp, "CT_LOCK_TAG": "F"}, "options": {"timeout": 5}}
            with stages.CtestLock(ctx) as lock:
                self.assertTrue(os.path.isfile(os.path.join(tmp, "ctest-lock.F")))
                self.assertEqual(lock.others(), [])
                self.assertEqual(lock.waited, 0.0)
            self.assertFalse(os.path.exists(os.path.join(tmp, "ctest-lock.F")))

    def test_another_tag_blocks_until_timeout(self):
        with tempfile.TemporaryDirectory() as tmp:
            with open(os.path.join(tmp, "ctest-lock.A"), "w") as f:
                f.write("1\n")
            ctx = {"env": {"CT_LOCK_DIR": tmp, "CT_LOCK_TAG": "F"}, "options": {"timeout": 0}}
            saved = stages.time.sleep
            stages.time.sleep = lambda s: None
            try:
                with self.assertRaises(TimeoutError):
                    with stages.CtestLock(ctx):
                        pass
            finally:
                stages.time.sleep = saved
            self.assertFalse(os.path.exists(os.path.join(tmp, "ctest-lock.F")))

    def test_no_lock_dir_means_no_locking(self):
        ctx = {"env": {}, "options": {"timeout": 5}}
        with stages.CtestLock(ctx) as lock:
            self.assertIsNone(lock.mine)
            self.assertEqual(lock.note()["waited_s"], 0.0)


class CtestTest(unittest.TestCase):
    def test_parse_junit(self):
        xml = """<?xml version="1.0"?><testsuite name="x" tests="4">
        <testcase name="A.Pass" status="run" time="0.1"/>
        <testcase name="A.Fail" status="run"><failure message="x"/></testcase>
        <testcase name="A.Skip" status="notrun"><skipped/></testcase>
        <testcase name="A.Fail2" status="fail"/></testsuite>"""
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "j.xml")
            with open(path, "w") as f:
                f.write(xml)
            r = ctest.parse_junit(path)
        self.assertEqual((r["total"], r["passed"], r["failed"], r["notrun"]), (4, 1, 2, 1))
        self.assertEqual(r["failures"], ["A.Fail", "A.Fail2"])
        self.assertIsNone(ctest.parse_junit(os.path.join(tmp, "missing.xml")))


def _image(path, width, height, pixel, patch=None):
    rows = []
    for y in range(height):
        row = bytearray(bytes(pixel) * width)
        if patch and patch[1] <= y < patch[3]:
            for x in range(patch[0], patch[2]):
                row[x * 3:x * 3 + 3] = bytes(patch[4])
        rows.append(bytes(row))
    images.write_png(path, width, height, rows)


class ImagesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_pure_reader_round_trip(self):
        path = os.path.join(self.tmp, "a.png")
        _image(path, 8, 4, (10, 20, 30), (2, 1, 5, 3, (200, 100, 0)))
        w, h, rows = images.read_png(path)
        self.assertEqual((w, h), (8, 4))
        self.assertEqual(rows[0][:3], bytes((10, 20, 30)))
        self.assertEqual(rows[1][6:9], bytes((200, 100, 0)))

    def test_sanity(self):
        flat = os.path.join(self.tmp, "flat.png")
        _image(flat, 64, 48, (0, 0, 0))
        self.assertFalse(images.sanity(flat)["ok"])
        self.assertTrue(images.sanity(flat)["single_colour"])
        busy = os.path.join(self.tmp, "busy.png")
        _image(busy, 64, 48, (0, 0, 0), (10, 10, 30, 30, (255, 255, 255)))
        self.assertTrue(images.sanity(busy)["ok"])
        self.assertFalse(images.sanity(os.path.join(self.tmp, "missing.png"))["ok"])

    def test_compare_identical_small_and_large_differences(self):
        a = os.path.join(self.tmp, "a.png")
        b = os.path.join(self.tmp, "b.png")
        c = os.path.join(self.tmp, "c.png")
        d = os.path.join(self.tmp, "d.png")
        _image(a, 100, 100, (100, 100, 100))
        _image(b, 100, 100, (100, 100, 100))
        _image(c, 100, 100, (101, 100, 100))
        _image(d, 100, 100, (100, 100, 100), (0, 0, 100, 10, (255, 255, 255)))
        same = images.compare(a, b)
        self.assertTrue(same["identical"] and same["pass"] and same["mean"] == 0)
        small = images.compare(a, c)
        self.assertTrue(small["pass"])
        self.assertAlmostEqual(small["mean"], 1 / 3, places=3)
        self.assertEqual(small["p99.5"], 1.0)
        big = images.compare(a, d)
        self.assertFalse(big["pass"])
        self.assertGreater(big["p99.5"], images.P995_LIMIT)
        _image(os.path.join(self.tmp, "e.png"), 50, 100, (0, 0, 0))
        self.assertFalse(images.compare(a, os.path.join(self.tmp, "e.png"))["same_size"])

    def test_pure_and_pil_agree(self):
        if images.Image is None:
            self.skipTest("PIL not installed")
        a = os.path.join(self.tmp, "a.png")
        _image(a, 32, 16, (1, 2, 3), (4, 4, 8, 8, (9, 8, 7)))
        w, h, rows = images.read_png(a)
        pil = images.Image.open(a).convert("RGB")
        self.assertEqual(pil.tobytes(), b"".join(rows))


class UtilTest(unittest.TestCase):
    def test_shell_join_quotes(self):
        self.assertEqual(util.shell_join(["a", "b c", "d'e"]), "a 'b c' 'd'\\''e'")

    def test_run_captures_exit_and_log(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = os.path.join(tmp, "x", "run.log")
            r = util.run([sys.executable, "-c", "print('hi'); raise SystemExit(3)"], log=log, timeout=30)
            self.assertEqual(r["exit"], 3)
            self.assertIn("hi", r["output"])
            with open(log) as f:
                self.assertIn("exit 3", f.read())
            r = util.run(["/nonexistent/binary"], timeout=5)
            self.assertEqual(r["exit"], 127)

    def test_read_env_sh(self):
        env = util.read_env_sh(os.path.join(HERE, "env.sh"))
        self.assertIn("CT_BUILD_DIR", env)
        self.assertTrue(any(f.startswith("-DCT_BUILD_TESTS=ON") for f in env["CMAKE_FLAGS"]))
        self.assertIn("libc++", env.get("CXXFLAGS", ""))

    def test_stages_json_is_valid(self):
        config = util.read_json(os.path.join(HERE, "stages.json"))
        for key in ("ctest", "capacity", "content", "web", "coop", "retail"):
            self.assertIn(key, config)
        self.assertEqual([s["name"] for s in config["retail"]["smokes"]], ["title", "town", "dungeon"])


if __name__ == "__main__":
    unittest.main(verbosity=1)
