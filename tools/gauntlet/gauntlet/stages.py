"""The gauntlet's stages, gate by gate (GAUNTLET.md).

Each stage is a function taking the run context and returning a report stage (report.stage). STAGES
lists them in gate order. A stage that has nothing to check returns not_run with the reason; one
whose check could not be set up returns error; pass and fail are the verdict of the check itself.
"""
import glob
import os
import shutil
import subprocess
import sys
import time
import zipfile

from . import baseline, ctest, envcheck, images, report, util


def _stage_dir(ctx, name):
    path = os.path.join(ctx["report_dir"], name)
    os.makedirs(path, exist_ok=True)
    return path


def _fixture_env(ctx, moonmod=False, build_dir=None):
    build = build_dir or ctx["build_dir"]
    dirs = ctx["config"]["ctest"]["fixture_dirs"]
    data, save = (os.path.join(build, d) for d in dirs)
    os.makedirs(data, exist_ok=True)
    os.makedirs(save, exist_ok=True)
    env = {"XDG_DATA_HOME": os.path.dirname(data), "SDL_VIDEO_DRIVER": "offscreen", "DC2_AUDIO": "off",
           "CXXFLAGS": ctx["env"].get("CXXFLAGS", "-stdlib=libc++"), "CXXSTDLIB": ctx["env"].get("CXXSTDLIB", "c++")}
    if moonmod:
        env["DC2_DATA"] = data
        env["DC2_SAVE"] = save
    icd = ctx["env"].get("CT_LAVAPIPE_ICD", "")
    if ctx["options"]["gpu"] and os.path.exists(icd):
        env["VK_ICD_FILENAMES"] = icd
        env["VK_DRIVER_FILES"] = icd
    return env


def _game_binary(ctx):
    for candidate in ("darkchronicle", "bin/darkchronicle", "port/darkchronicle"):
        path = os.path.join(ctx["build_dir"], candidate)
        if os.path.isfile(path) and os.access(path, os.X_OK):
            return path
    return None


def _tests_binary(ctx, build_dir=None):
    path = os.path.join(build_dir or ctx["build_dir"], "darkchronicle_tests")
    return path if os.path.isfile(path) else None


def disk_guard(ctx, name, gate, path=None, tests=False):
    """An error stage when the disk under path (the build directory) has less than --min-free-gb free (a build,
    the toolkit's cargo build) or --min-free-gb-tests (tests=True: a ctest run of binaries already built, which
    writes logs and temporary saves, not objects): a build or a test run on a full disk fails in ways that look
    like regressions (a ctest run on a full disk once reported 189 'No space left on device' failures), so the
    stage does not start. None when there is room."""
    minimum = ctx["options"].get("min_free_gb_tests", 1.0) if tests else ctx["options"].get("min_free_gb", 4.0)
    free = util.disk_free_gb(path or ctx["build_dir"])
    if free is None or not minimum or free >= minimum:
        return None
    return report.stage(name, gate, "error", "only %.1f GB free on the disk holding %s (the gauntlet wants %.1f GB, "
                        "%s): free space first; the stage did not start" % (free, path or ctx["build_dir"], minimum,
                                                                             "--min-free-gb-tests" if tests else "--min-free-gb"),
                        details={"free_gb": round(free, 2), "min_free_gb": minimum})


def _iso_time(stamp):
    return time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime(stamp))


def binary_info(ctx, path, source_dirs=("port",), skip=("build", "web", ".cache", "node_modules", "third_party"), root=None):
    """When the binary was linked and how many source files under port/ are newer: a stale binary is
    reported, never silently tested as if it were the tree."""
    if not path or not os.path.isfile(path):
        return None
    root = root or ctx["root"]
    linked = os.path.getmtime(path)
    newer = []
    for rel in source_dirs:
        top = os.path.join(root, rel)
        for directory, dirs, files in os.walk(top):
            dirs[:] = [d for d in dirs if d not in skip]
            for f in files:
                if f.endswith((".c", ".cc", ".cpp", ".h", ".hpp", ".rs", ".txt", ".cmake", ".json", ".lua", ".luau")):
                    full = os.path.join(directory, f)
                    try:
                        if os.path.getmtime(full) > linked:
                            newer.append(os.path.relpath(full, root))
                    except OSError:
                        pass
    newer.sort()
    return {"path": path, "linked": _iso_time(linked), "newer_sources": len(newer), "newer_sample": newer[:20],
            "stale": bool(newer)}


def _binary_note(info):
    if not info:
        return ""
    note = "; binary linked %s" % info["linked"]
    if info["stale"]:
        note += " (%d source file(s) are newer: stale build)" % info["newer_sources"]
    return note


class CtestLock:
    """Agents sharing one build directory take turns at ctest: <CT_LOCK_DIR>/ctest-lock.<tag> is held while a
    ctest stage runs, and a stage waits (polling every 30 s, up to the stage timeout) while another tag's file
    exists. No lock directory means no locking."""

    def __init__(self, ctx):
        self.directory = ctx["env"].get("CT_LOCK_DIR") or os.environ.get("CT_LOCK_DIR") or ""
        self.tag = ctx["env"].get("CT_LOCK_TAG") or os.environ.get("CT_LOCK_TAG") or "F"
        self.timeout = ctx["options"]["timeout"]
        self.waited = 0.0
        self.waited_for = []
        self.mine = os.path.join(self.directory, "ctest-lock." + self.tag) if self.directory else None

    def others(self):
        if not self.directory or not os.path.isdir(self.directory):
            return []
        return sorted(f for f in os.listdir(self.directory) if f.startswith("ctest-lock.") and f != "ctest-lock." + self.tag)

    def __enter__(self):
        if not self.mine:
            return self
        started = time.monotonic()
        while self.others():
            self.waited_for = sorted(set(self.waited_for) | set(self.others()))
            if time.monotonic() - started > self.timeout:
                raise TimeoutError("waited %ds for %s in %s" % (self.timeout, ", ".join(self.others()), self.directory))
            time.sleep(30)
        self.waited = round(time.monotonic() - started, 1)
        os.makedirs(self.directory, exist_ok=True)
        with open(self.mine, "w") as f:
            f.write("%d %s\n" % (os.getpid(), _iso_time(time.time())))
        return self

    def __exit__(self, *exc):
        if self.mine:
            try:
                os.remove(self.mine)
            except OSError:
                pass
        return False

    def note(self):
        return {"lock_dir": self.directory, "tag": self.tag, "waited_s": self.waited, "waited_for": self.waited_for}


def _run_ctest(ctx, log, junit, build_dir=None, source_root=None, **kw):
    """ctest.run under the lock, with the test binary's link time before and after."""
    build_dir = build_dir or ctx["build_dir"]
    before = binary_info(ctx, _tests_binary(ctx, build_dir), root=source_root)
    try:
        with CtestLock(ctx) as lock:
            result = ctest.run(build_dir, log, junit, **kw)
    except TimeoutError as e:
        return {"exit": -1, "timed_out": True, "duration": 0.0, "output": "[gauntlet] ctest lock: %s\n" % e, "command": "ctest",
                "log": log, "results": None, "binary_before": before, "binary_after": before, "lock": None, "lock_timeout": str(e)}
    result["binary_before"] = before
    result["binary_after"] = binary_info(ctx, _tests_binary(ctx, build_dir), root=source_root)
    result["lock"] = lock.note()
    return result


def contaminated(result):
    """Why a ctest run proves nothing, or None: the test binary was relinked while it ran (another agent
    building: "Could not find executable" for the cases that started during the link, and the rest ran
    against two binaries), or the disk filled up under it ("No space left on device")."""
    before, after = result.get("binary_before"), result.get("binary_after")
    output = result.get("output", "")
    why = []
    missing = output.count("Could not find executable")
    if missing:
        why.append("%d case(s) could not find the executable" % missing)
    if after and before and after["linked"] != before["linked"]:
        why.append("binary linked %s at the start, %s at the end" % (before["linked"], after["linked"]))
    full = output.count("No space left on device")
    if full:
        why.append("the disk filled up during the run (%d 'No space left on device' line(s))" % full)
    return "; ".join(why) or None


def _ctest_stage(ctx, name, gate, regex=None, exclude=None, label_exclude=None, require_all=False,
                 baseline_key=None, none_summary=None, moonmod=False):
    if not _tests_binary(ctx):
        return report.stage(name, gate, "error", "no darkchronicle_tests in %s: build first (G0 build)" % ctx["build_dir"])
    full = disk_guard(ctx, name, gate, tests=True)
    if full:
        return full
    out = _stage_dir(ctx, name)
    result = _run_ctest(ctx, os.path.join(out, "ctest.log"), os.path.join(out, "ctest.xml"), regex=regex,
                        exclude_regex=exclude, label_exclude=label_exclude, jobs=ctx["options"]["jobs"],
                        env=_fixture_env(ctx, moonmod), timeout=ctx["options"]["timeout"], xvfb=ctx["options"]["gpu"])
    binary = result["binary_before"]
    if result.get("lock_timeout"):
        return report.stage(name, gate, "error", "another agent held the ctest lock too long: " + result["lock_timeout"])
    results = result["results"]
    evidence = [result["log"]] + ([os.path.join(out, "ctest.xml")] if results else [])
    if results is None:
        why = contaminated(result)
        return report.stage(name, gate, "error", ("ctest wrote no results (%s): " % why if why else "ctest wrote no results: ")
                            + util.tail(result["output"], 5),
                            result["duration"], result["command"], result["log"], evidence)
    # A binary relinked while ctest runs (another agent building) gives "Could not find executable" for the
    # cases that started during the link and tests two different binaries; a full disk fails cases that write
    # files. No verdict can be drawn from either.
    after = result["binary_after"]
    why = contaminated(result)
    if why:
        return report.stage(name, gate, "error", "the run proves nothing (%s): rerun when the build is quiet and the "
                            "disk has room; %d cases: %d passed, %d failed, %d not run" % (
                                why, results["total"], results["passed"], results["failed"], results["notrun"]),
                            result["duration"], result["command"], result["log"], evidence,
                            details={"counts": {k: results[k] for k in ("total", "passed", "failed", "notrun")},
                                     "failures": results["failures"], "tests_binary": binary, "tests_binary_after": after,
                                     "contaminated": why, "lock": result["lock"]})
    if results["total"] == 0:
        return report.stage(name, gate, "not_run", none_summary or "no test matches %r in this build" % regex,
                            result["duration"], result["command"], result["log"], evidence)
    counts = "%d cases: %d passed, %d failed, %d not run" % (results["total"], results["passed"], results["failed"],
                                                           results["notrun"])
    details = {"counts": {k: results[k] for k in ("total", "passed", "failed", "notrun")}, "failures": results["failures"],
               "tests_binary": binary, "tests_binary_after": after, "lock": result["lock"]}
    if baseline_key:
        path = ctx["baseline_path"]
        data = baseline.load(path)
        ran = [c["name"] for c in results["cases"] if c["status"] != "notrun"]
        delta = baseline.diff(results["failures"], ran, data, baseline_key)
        if ctx["options"]["update_baseline"]:
            previous = {k: delta[k] for k in ("new", "known", "fixed", "has_baseline")}
            data = baseline.update(path, baseline_key, results["failures"], details["counts"],
                                   head=ctx["repos"].get("ChronicleTwo", {}).get("head"),
                                   note="written by run.py --update-baseline")
            delta = baseline.diff(results["failures"], ran, data, baseline_key)
            delta["written"] = path
            delta["before_rewrite"] = previous
        if not delta["has_baseline"]:
            status = "error"
            summary = counts + "; no baseline for %r in %s: run once with --update-baseline" % (baseline_key, path)
        else:
            status = "fail" if delta["new"] else "pass"
            summary = counts + "; %d new failure(s), %d known, %d fixed against %s" % (
                len(delta["new"]), len(delta["known"]), len(delta["fixed"]), os.path.basename(path))
            if delta.get("unseen"):
                summary += " (%d known did not run: skipped or absent now, neither fixed nor failing)" % len(delta["unseen"])
            if "written" in delta:
                summary += " (baseline rewritten: %d were new, %d fixed before)" % (
                    len(delta["before_rewrite"]["new"]), len(delta["before_rewrite"]["fixed"]))
        return report.stage(name, gate, status, summary + _binary_note(binary), result["duration"], result["command"],
                            result["log"], evidence, baseline=delta, details=details)
    status = "pass" if results["failed"] == 0 else "fail"
    summary = counts + ("" if status == "pass" else "; failing: " + ", ".join(results["failures"][:8]))
    return report.stage(name, gate, status, summary + _binary_note(binary), result["duration"], result["command"],
                        result["log"], evidence, details=details)


def stage_env(ctx):
    started = time.monotonic()
    checks = envcheck.check(ctx["env"])
    summary, ok = envcheck.summary(checks)
    out = _stage_dir(ctx, "env")
    path = os.path.join(out, "env.json")
    util.write_json(path, {"checks": checks, "env_sh": ctx["env"]})
    with open(os.path.join(out, "env.txt"), "w") as f:
        for c in checks:
            f.write("%-22s %-4s %s %s\n" % (c["name"], "ok" if c["ok"] else "MISS", "required" if c["required"] else "optional", c["detail"]))
    return report.stage("env", "G0", "pass" if ok else "fail", summary, time.monotonic() - started,
                        "python3 -I tools/gauntlet/run.py --stage env", None, [path, os.path.join(out, "env.txt")],
                        details={"checks": checks})


def stage_build(ctx):
    out = _stage_dir(ctx, "build")
    env_sh = ctx["env_sh"]
    build = ctx["build_dir"]
    evidence = []
    if ctx["options"].get("no_build"):
        info = binary_info(ctx, _tests_binary(ctx))
        if not info:
            return report.stage("build", "G0", "error", "--no-build but there is no darkchronicle_tests in %s" % build)
        return report.stage("build", "G0", "not_run", "skipped by --no-build; the stages test the existing binaries"
                            + _binary_note(info), 0.0, ". tools/gauntlet/env.sh && ct_build", None, [],
                            details={"tests": info, "game": binary_info(ctx, _game_binary(ctx))})
    full = disk_guard(ctx, "build", "G0")
    if full:
        return full
    if ctx["options"]["clean"] and os.path.isdir(build):
        shutil.rmtree(build)
    total = 0.0
    commands = []
    if not os.path.exists(os.path.join(build, "CMakeCache.txt")):
        configure = util.run(["bash", "-c", '. "$1" && ct_configure', "_", env_sh], log=os.path.join(out, "configure.log"),
                             cwd=ctx["root"], env={"CT_BUILD_DIR": build}, timeout=ctx["options"]["timeout"])
        total += configure["duration"]
        commands.append(". tools/gauntlet/env.sh && ct_configure")
        evidence.append(configure["log"])
        if configure["exit"] != 0:
            return report.stage("build", "G0", "fail", "configure failed: " + util.tail(configure["output"], 3), total,
                                " && ".join(commands), configure["log"], evidence)
    result = util.run(["bash", "-c", '. "$1" && shift && ct_build "$@"', "_", env_sh] + ctx["options"]["build_targets"],
                      log=os.path.join(out, "build.log"), cwd=ctx["root"], env={"CT_BUILD_DIR": build},
                      timeout=ctx["options"]["build_timeout"])
    total += result["duration"]
    commands.append(". tools/gauntlet/env.sh && ct_build" + "".join(" " + t for t in ctx["options"]["build_targets"]))
    evidence.append(result["log"])
    errors = [line for line in result["output"].splitlines() if "error:" in line or line.startswith("FAILED:")]
    with open(os.path.join(out, "errors.txt"), "w") as f:
        f.write("\n".join(errors) + "\n")
    evidence.append(os.path.join(out, "errors.txt"))
    if result["exit"] != 0:
        return report.stage("build", "G0", "fail", "build failed (%d error lines): %s" % (len(errors), (errors or [util.tail(result["output"], 1)])[0][:200]),
                            total, " && ".join(commands), result["log"], evidence)
    game = _game_binary(ctx)
    tests = _tests_binary(ctx)
    missing = [n for n, p in (("darkchronicle", game), ("darkchronicle_tests", tests)) if not p]
    if missing and not ctx["options"]["build_targets"]:
        return report.stage("build", "G0", "fail", "build exited 0 but produced no " + ", ".join(missing), total,
                            " && ".join(commands), result["log"], evidence)
    return report.stage("build", "G0", "pass", "built %s (%s)" % (build, "incremental" if not ctx["options"]["clean"] else "clean"),
                        total, " && ".join(commands), result["log"], evidence, details={"game": game, "tests": tests})


def stage_ctest(ctx):
    label_exclude = None if ctx["options"]["gpu"] else ctx["config"]["ctest"]["cpu_label_exclude"]
    return _ctest_stage(ctx, "ctest", "G0", label_exclude=label_exclude,
                        baseline_key="ctest-gpu" if ctx["options"]["gpu"] else "ctest-cpu")


def stage_capacity(ctx):
    return _ctest_stage(ctx, "capacity", "G1", regex=ctx["config"]["capacity"]["ctest_regex"], require_all=True,
                        none_summary="no Capacity* tests in the build yet (work package A adds them)")


def fold(sub_stages):
    """One status for a group of stages run elsewhere (the toolkit's runner): fail if any failed, else error
    if any errored, else pass if any ran, else not_run. not_run is never a pass."""
    statuses = [s["status"] for s in sub_stages]
    if "fail" in statuses:
        return "fail"
    if "error" in statuses:
        return "error"
    if "pass" in statuses:
        return "pass"
    return "not_run"


def _toolkit_stage(ctx, name, runner_args):
    """Run DC2-Toolkit's own gauntlet (tools/gauntlet/run.py there) and fold its report into one stage.

    The toolkit runner writes the same report schema under <report>/<name>/toolkit/, imports the report and
    baseline code from this tree (CHRONICLE_TWO), and keeps its own baseline file; --update-baseline is not
    passed through, so the toolkit's known failures are only rewritten from its own runner on purpose."""
    toolkit = ctx["repos"].get("DC2-Toolkit", {})
    if not toolkit.get("present"):
        return report.stage(name, "G1", "not_run", "DC2-Toolkit is not beside this tree")
    runner = os.path.join(toolkit["path"], ctx["config"]["toolkit"]["runner"])
    if not os.path.isfile(runner):
        return report.stage(name, "G1", "not_run", "DC2-Toolkit has no %s" % ctx["config"]["toolkit"]["runner"])
    full = disk_guard(ctx, name, "G1", toolkit["path"])
    if full:
        return full
    out = _stage_dir(ctx, name)
    sub_report = os.path.join(out, "toolkit")
    cmd = [sys.executable, "-I", runner, "--json", "--keep-going", "--report-dir", sub_report,
           "--timeout", str(ctx["options"]["timeout"]), "--min-free-gb", str(ctx["options"].get("min_free_gb", 4.0))] + list(runner_args)
    if ctx["options"].get("iso"):
        cmd += ["--iso", ctx["options"]["iso"]]
    result = util.run(cmd, log=os.path.join(out, "runner.log"), cwd=toolkit["path"], env={"CHRONICLE_TWO": ctx["root"]},
                      timeout=ctx["options"]["build_timeout"])
    path = os.path.join(sub_report, "report.json")
    sub = util.read_json(path)
    reproduce = "(cd %s && CHRONICLE_TWO=%s %s)" % (toolkit["path"], ctx["root"], util.shell_join(
        ["python3", "-I", os.path.relpath(runner, toolkit["path"])] + list(runner_args) + (["--iso", ctx["options"]["iso"]] if ctx["options"].get("iso") else [])))
    if not sub or report.validate(sub):
        return report.stage(name, "G1", "error", "the toolkit runner wrote no valid report (exit %d): %s" % (
            result["exit"], util.tail(result["output"], 3)), result["duration"], reproduce, result["log"], [result["log"]])
    stages_ = sub["stages"]
    status = fold(stages_)
    parts = []
    for s in stages_:
        b = s.get("baseline") or {}
        extra = ""
        if b.get("new") or b.get("known") or b.get("fixed"):
            extra = " (%d new, %d known, %d fixed)" % (len(b.get("new", [])), len(b.get("known", [])), len(b.get("fixed", [])))
        parts.append("%s %s%s" % (s["name"], s["status"], extra))
    summary = "toolkit: " + ", ".join(parts)
    if status == "not_run":
        summary += "; " + "; ".join("%s: %s" % (s["name"], s["summary"]) for s in stages_)[:400]
    elif status in ("fail", "error"):
        summary += "; " + "; ".join("%s: %s" % (s["name"], s["summary"]) for s in stages_ if s["status"] in ("fail", "error"))[:600]
    evidence = [path, os.path.join(sub_report, "report.md")] + [e for s in stages_ for e in s.get("evidence", [])]
    return report.stage(name, "G1", status, summary, result["duration"], reproduce, result["log"], evidence,
                        details={"toolkit_report": path, "stages": [{k: s[k] for k in ("name", "gate", "status", "summary", "baseline")}
                                                                    for s in stages_], "exit": result["exit"]})


def stage_toolkit(ctx):
    return _toolkit_stage(ctx, "toolkit", ctx["config"]["toolkit"]["args"])


def stage_toolkit_limits(ctx):
    return _toolkit_stage(ctx, "toolkit-limits", ctx["config"]["toolkit"]["limits_args"])


def stage_content(ctx):
    exclude = None if ctx["options"]["gpu"] else ctx["config"]["content"]["ctest_exclude"]
    return _ctest_stage(ctx, "content", "G2", regex=ctx["config"]["content"]["ctest_regex"], exclude=exclude, require_all=True,
                        moonmod=True)


def _lua(ctx, out):
    found = util.which("lua", "lua5.5", "lua5.4")
    if found:
        return found, None
    cache = os.path.join(ctx["cache_dir"], "lua", "lua")
    if os.path.isfile(cache):
        return cache, None
    moonmod = ctx["repos"].get("MoonMod", {})
    src = os.path.join(moonmod.get("path", ""), ctx["config"]["content"]["lua_vendor"])
    if not os.path.isdir(src):
        return None, "no lua on PATH and no %s to build" % src
    os.makedirs(os.path.dirname(cache), exist_ok=True)
    sources = sorted(f for f in glob.glob(os.path.join(src, "*.c")) if not f.endswith("luac.c"))
    result = util.run([ctx["env"].get("CT_CC", "clang"), "-O2", "-DLUA_USE_LINUX", "-o", cache] + sources + ["-lm", "-ldl"],
                      log=os.path.join(out, "build-lua.log"), timeout=600)
    if result["exit"] != 0:
        return None, "building the vendored Lua failed: " + util.tail(result["output"], 3)
    return cache, None


def stage_lua_harness(ctx):
    moonmod = ctx["repos"].get("MoonMod", {})
    if not moonmod.get("present"):
        return report.stage("lua-harness", "G2", "not_run", "MoonMod is not beside this tree")
    out = _stage_dir(ctx, "lua-harness")
    lua, problem = _lua(ctx, out)
    if not lua:
        return report.stage("lua-harness", "G2", "error", problem)
    tests = sorted(glob.glob(os.path.join(moonmod["path"], ctx["config"]["content"]["lua_tests_glob"])))
    if not tests:
        return report.stage("lua-harness", "G2", "not_run", "no Lua harness under MoonMod/tools/tests")
    started = time.monotonic()
    failed = []
    evidence = []
    commands = []
    for test in tests:
        name = os.path.relpath(test, moonmod["path"]).replace("/", "_")
        result = util.run([lua, test], log=os.path.join(out, name + ".log"), cwd=moonmod["path"], timeout=600)
        evidence.append(result["log"])
        commands.append("(cd %s && %s %s)" % (moonmod["path"], lua, os.path.relpath(test, moonmod["path"])))
        if result["exit"] != 0:
            failed.append(os.path.relpath(test, moonmod["path"]))
    summary = "%d harness(es) with %s, %d failed" % (len(tests), lua, len(failed)) + ("" if not failed else ": " + ", ".join(failed))
    return report.stage("lua-harness", "G2", "fail" if failed else "pass", summary, time.monotonic() - started,
                        " ; ".join(commands), None, evidence, details={"failed": failed, "lua": lua})


def _web_build(ctx):
    build = ctx["options"]["web_build"] or os.path.join(ctx["root"], ctx["config"]["web"]["build_dir"])
    for name in ctx["config"]["web"]["game_js"]:
        if os.path.isfile(os.path.join(build, name)):
            return build, name
    return build, None


def stage_web_node(ctx):
    build, game_js = _web_build(ctx)
    if not game_js:
        return report.stage("web-node", "G3", "not_run", "no web build at %s (work package C)" % build)
    out = _stage_dir(ctx, "web-node")
    save = os.path.join(out, "smoke-save")
    os.makedirs(save, exist_ok=True)
    node = ctx["env"].get("CT_NODE") if os.path.exists(ctx["env"].get("CT_NODE", "")) else "node"
    cmd = [node, game_js] + ctx["config"]["web"]["node_args"] + ["--save", save]
    result = util.run(cmd, log=os.path.join(out, "node.log"), cwd=build, timeout=180)
    fatal = [p for p in ctx["config"]["web"]["fatal_patterns"] if p in result["output"]]
    ok = result["exit"] in ctx["config"]["web"]["node_exit_codes"] and not fatal
    summary = "node %s exited %d%s" % (game_js, result["exit"], ("; fatal: " + ", ".join(fatal)) if fatal else "")
    return report.stage("web-node", "G3", "pass" if ok else "fail", summary, result["duration"], result["command"],
                        result["log"], [result["log"]])


def stage_web_browser(ctx):
    build, game_js = _web_build(ctx)
    if not game_js:
        return report.stage("web-browser", "G3", "not_run", "no web build at %s (work package C)" % build)
    if not os.path.isfile(os.path.join(build, "index.html")):
        return report.stage("web-browser", "G3", "not_run", "the web build has no index.html page")
    node = ctx["env"].get("CT_NODE") if os.path.exists(ctx["env"].get("CT_NODE", "")) else "node"
    probe = util.run([node, "-e", "require('playwright')"], timeout=60)
    if probe["exit"] != 0:
        return report.stage("web-browser", "G3", "error", "Playwright is not installed for node")
    out = _stage_dir(ctx, "web-browser")
    archive = os.path.join(build, "smoke-data.zip")
    with zipfile.ZipFile(archive, "w") as z:
        z.writestr("data/smoke.txt", "not the game data\n")
    # One run per renderer the build has: WebGPU (darkchronicle.js) and, when built, WebGL2 (darkchronicle_gl.js).
    renderers = [r for r in ctx["config"]["web"]["renderers"] if os.path.isfile(os.path.join(build, r["game_js"]))] or [
        {"name": "webgpu", "query": "", "game_js": game_js}]
    port = util.free_port()
    server = subprocess.Popen([sys.executable, "-m", "http.server", str(port), "--bind", "127.0.0.1"], cwd=build,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    runs = []
    try:
        util.wait_for(lambda: _url_ok("http://127.0.0.1:%d/index.html" % port), 20)
        npm_root = util.run(["npm", "root", "-g"], timeout=60)["output"].strip()
        for renderer in renderers:
            log = os.path.join(out, "browser-%s.log" % renderer["name"])
            cmd = [node, os.path.join(ctx["gauntlet_dir"], "web", "browser_smoke.cjs"),
                   "http://127.0.0.1:%d/index.html%s" % (port, renderer["query"]), "/smoke-data.zip", log] + ctx["config"]["web"]["node_args"]
            result = util.run(cmd, log=os.path.join(out, "smoke-run-%s.log" % renderer["name"]), cwd=out, env={"NPM_GLOBAL_ROOT": npm_root},
                              timeout=400)
            page_log = ""
            try:
                with open(log, encoding="utf-8") as f:
                    page_log = f.read()
            except OSError:
                pass
            exit_lines = [line for line in page_log.splitlines() if line.startswith("exit=")]
            game_exit = int(exit_lines[-1].split("=")[1]) if exit_lines and exit_lines[-1][5:].strip().lstrip("-").isdigit() else None
            fatal = [p for p in ctx["config"]["web"]["fatal_patterns"] if p in page_log]
            runs.append({"renderer": renderer["name"], "driver_exit": result["exit"], "game_exit": game_exit, "fatal": fatal,
                         "ok": result["exit"] == 0 and game_exit in ctx["config"]["web"]["browser_exit_codes"] and not fatal,
                         "command": result["command"], "log": result["log"], "page_log": log, "duration": result["duration"]})
    finally:
        server.terminate()
    ok = all(r["ok"] for r in runs)
    summary = "Chromium smoke: " + "; ".join("%s driver exit %d, game exit %s%s" % (
        r["renderer"], r["driver_exit"], r["game_exit"], (", fatal: " + ", ".join(r["fatal"])) if r["fatal"] else "") for r in runs)
    return report.stage("web-browser", "G3", "pass" if ok else "fail", summary, sum(r["duration"] for r in runs),
                        " ; ".join(r["command"] for r in runs), runs[0]["log"], [p for r in runs for p in (r["log"], r["page_log"])],
                        details={"runs": [{k: r[k] for k in ("renderer", "driver_exit", "game_exit", "fatal", "ok")} for r in runs]})


def _url_ok(url):
    import urllib.request
    try:
        with urllib.request.urlopen(url, timeout=2) as r:
            return r.status < 500
    except Exception:
        return False


def _relay_source(ctx):
    for rel in ctx["config"]["coop"]["relay_dirs"]:
        path = os.path.normpath(os.path.join(ctx["root"], rel))
        if os.path.isfile(os.path.join(path, "server.mjs")) and os.path.isdir(os.path.join(path, "src")):
            return path
    return None


def _source_root(build_dir):
    """The source tree a CMake build directory was configured from (CMakeCache.txt), or None."""
    try:
        with open(os.path.join(build_dir, "CMakeCache.txt"), encoding="utf-8", errors="replace") as f:
            for line in f:
                if line.startswith("CMAKE_HOME_DIRECTORY:"):
                    return line.split("=", 1)[1].strip() or None
    except OSError:
        pass
    return None


def _coop_build(ctx):
    """The build the co-op stage tests: --coop-build (package D's tree until it is merged) or this tree's.
    Returns (build_dir, source_root, reason it cannot be used or None)."""
    explicit = ctx["options"].get("coop_build")
    build = os.path.abspath(explicit) if explicit else ctx["build_dir"]
    root = (_source_root(build) if explicit else None) or (os.path.dirname(os.path.dirname(build)) if explicit else ctx["root"])
    if explicit and not os.path.isdir(build):
        return build, root, "no co-op build directory at %s" % build
    return build, root, None


def _coop_engine(ctx, out, build, root, url, relay, node, evidence, commands):
    """The engine side of G4 against the relay at url: ctest MoonModNet|MoonModCoop in the build, then
    tools/coop/two_games.py from its source tree (two headless games meeting through the relay). Returns a
    list of parts {name, status, summary}."""
    parts = []
    if not _tests_binary(ctx, build):
        parts.append({"name": "ctest", "status": "not_run", "summary": "no darkchronicle_tests in %s (not built yet)" % build})
    else:
        env = _fixture_env(ctx, moonmod=True, build_dir=build)
        for name in ctx["config"]["coop"]["relay_env"]:
            env[name] = url
        engine = _run_ctest(ctx, os.path.join(out, "ctest.log"), os.path.join(out, "ctest.xml"), build_dir=build, source_root=root,
                            regex=ctx["config"]["coop"]["ctest_regex"], jobs=1, env=env, timeout=ctx["options"]["timeout"])
        evidence.append(engine["log"])
        commands.append(engine["command"] + " (with %s=%s)" % (ctx["config"]["coop"]["relay_env"][0], url))
        results = engine["results"]
        why = contaminated(engine)
        if engine.get("lock_timeout"):
            parts.append({"name": "ctest", "status": "error", "summary": "ctest lock: " + engine["lock_timeout"]})
        elif results is None:
            parts.append({"name": "ctest", "status": "error", "summary": "ctest wrote no results: " + util.tail(engine["output"], 2)})
        elif why:
            parts.append({"name": "ctest", "status": "error", "summary": "the run proves nothing (%s)" % why})
        elif results["total"] == 0:
            parts.append({"name": "ctest", "status": "not_run", "summary": "no engine co-op tests (%r) in %s yet (work package D)" % (
                ctx["config"]["coop"]["ctest_regex"], build)})
        else:
            ran = results["total"] - results["notrun"]
            status = "fail" if results["failed"] else ("pass" if ran else "not_run")
            parts.append({"name": "ctest", "status": status, "summary": "%d engine case(s): %d passed, %d failed, %d skipped%s" % (
                results["total"], results["passed"], results["failed"], results["notrun"],
                (": " + ", ".join(results["failures"][:6])) if results["failures"] else ""),
                "failures": results["failures"], "binary": engine["binary_before"]})
    script = os.path.join(root or "", ctx["config"]["coop"]["two_games"])
    if not root or not os.path.isfile(script):
        parts.append({"name": "two-games", "status": "not_run", "summary": "no %s in %s" % (ctx["config"]["coop"]["two_games"], root)})
    elif not _game_binary({"build_dir": build}):
        parts.append({"name": "two-games", "status": "not_run", "summary": "no darkchronicle in %s (not built yet)" % build})
    else:
        work = os.path.join(out, "two-games")
        shutil.rmtree(work, ignore_errors=True)
        cmd = [sys.executable, "-I", script, "--build", build, "--relay-dir", relay, "--node", node, "--work", work]
        result = util.run(cmd, log=os.path.join(out, "two-games.log"), cwd=root, env={"CT_NODE": node}, timeout=ctx["options"]["timeout"])
        evidence.append(result["log"])
        commands.append(result["command"])
        last = util.tail(result["output"], 1)
        status = {0: "pass", 1: "fail", 2: "not_run"}.get(result["exit"], "error")
        parts.append({"name": "two-games", "status": status, "summary": "two_games.py exit %d: %s" % (result["exit"], last[:200])})
    return parts


def stage_coop(ctx):
    source = _relay_source(ctx)
    if not source:
        return report.stage("coop", "G4", "not_run", "no relay (coop/relay/server.mjs) in this tree or DC_Web")
    build, root, problem = _coop_build(ctx)
    if problem:
        return report.stage("coop", "G4", "not_run", problem + " (--coop-build names package D's build)")
    if _tests_binary(ctx, build):
        full = disk_guard(ctx, "coop", "G4", build, tests=True)
        if full:
            return full
    out = _stage_dir(ctx, "coop")
    relay = os.path.join(ctx["cache_dir"], "relay")
    os.makedirs(relay, exist_ok=True)
    shutil.copy2(os.path.join(source, "server.mjs"), relay)
    if os.path.isdir(os.path.join(relay, "src")):
        shutil.rmtree(os.path.join(relay, "src"))
    shutil.copytree(os.path.join(source, "src"), os.path.join(relay, "src"))
    shutil.copy2(os.path.join(ctx["gauntlet_dir"], "coop", "handshake.mjs"), relay)
    node = ctx["env"].get("CT_NODE") if os.path.exists(ctx["env"].get("CT_NODE", "")) else "node"
    evidence = []
    if not os.path.isdir(os.path.join(relay, "node_modules", "ws")):
        with open(os.path.join(relay, "package.json"), "w") as f:
            f.write('{"name": "gauntlet-relay", "private": true, "type": "module"}\n')
        install = util.run(["npm", "install", "--no-audit", "--no-fund", "ws@" + ctx["config"]["coop"]["ws_version"]],
                           log=os.path.join(out, "npm-install.log"), cwd=relay, timeout=300)
        evidence.append(install["log"])
        if install["exit"] != 0:
            return report.stage("coop", "G4", "error", "npm install ws failed: " + util.tail(install["output"], 2),
                                install["duration"], install["command"], install["log"], evidence)
    port = util.free_port()
    url = "ws://127.0.0.1:%d" % port
    relay_log = open(os.path.join(out, "relay.log"), "w")
    evidence.append(relay_log.name)
    server = subprocess.Popen([node, "server.mjs", "--port", str(port), "--host", "127.0.0.1"], cwd=relay,
                              stdout=relay_log, stderr=subprocess.STDOUT)
    started = time.monotonic()
    commands = ["(cd %s && node server.mjs --port %d --host 127.0.0.1 &)" % (relay, port), "(cd %s && node handshake.mjs %s)" % (relay, url)]
    try:
        if not util.wait_for(lambda: _url_ok("http://127.0.0.1:%d/health" % port), 20):
            return report.stage("coop", "G4", "error", "the relay did not answer /health on port %d" % port,
                                time.monotonic() - started, "node server.mjs --port %d" % port, relay_log.name, evidence)
        handshake = util.run([node, "handshake.mjs", url], log=os.path.join(out, "handshake.log"), cwd=relay, timeout=60)
        evidence.append(handshake["log"])
        if handshake["exit"] != 0:
            return report.stage("coop", "G4", "fail", "host/join handshake failed: " + util.tail(handshake["output"], 2),
                                time.monotonic() - started, " ; ".join(commands), handshake["log"], evidence)
        parts = _coop_engine(ctx, out, build, root, url, relay, node, evidence, commands)
    finally:
        server.terminate()
        try:
            server.wait(10)
        except subprocess.TimeoutExpired:
            server.kill()
        relay_log.close()
    duration = time.monotonic() - started
    status = fold(parts)
    summary = "relay host/join handshake OK on %s; build %s; %s" % (url, build, "; ".join("%s %s: %s" % (
        p["name"], p["status"], p["summary"]) for p in parts))
    return report.stage("coop", "G4", status, summary, duration, " ; ".join(commands), evidence[-1] if evidence else None, evidence,
                        details={"build": build, "source_root": root, "parts": parts})


def _data_root(ctx):
    marker = ctx["config"]["retail"]["data_marker"]
    candidates = [ctx["options"]["data"], os.environ.get("DC2_DATA"), os.path.join(ctx["root"], "port", "run", "data"),
                  os.path.join(ctx["cache_dir"], "retail", "data")]
    for root in candidates:
        if root and os.path.isdir(root) and os.path.exists(os.path.join(os.path.dirname(root.rstrip("/")), marker)):
            return root, None
    iso = ctx["options"]["iso"] or os.environ.get("DC2_ISO")
    if not iso:
        found = sorted(glob.glob(os.path.join(ctx["root"], ctx["config"]["retail"]["iso_glob"])))
        iso = found[0] if found else None
    if not iso or not os.path.isfile(iso):
        return None, "no retail data: set DC2_DATA to an extracted root, or DC2_ISO / rom/pal/*.iso to a disc image"
    target = os.path.join(ctx["cache_dir"], "retail", "data")
    dc2data = _dc2data(ctx)
    if not dc2data:
        return None, "disc %s present but no dc2data in the build to extract it" % iso
    out = _stage_dir(ctx, "retail-extract")
    result = util.run([dc2data, "extract", iso, target], log=os.path.join(out, "extract.log"), timeout=7200)
    if result["exit"] != 0:
        return None, "dc2data extract failed (%s)" % result["log"]
    return target, None


def _dc2data(ctx):
    for path in glob.glob(os.path.join(ctx["build_dir"], "**", "dc2data"), recursive=True):
        if os.path.isfile(path) and os.access(path, os.X_OK):
            return path
    return None


def _run_smoke(ctx, out, smoke, data, label=None, seed=None):
    game = _game_binary(ctx)
    name = label or smoke["name"]
    save = os.path.join(out, name + "-save")
    shutil.rmtree(save, ignore_errors=True)
    os.makedirs(save)
    shot = os.path.join(out, name + ".png")
    args = []
    for word in smoke["args"]:
        if word.endswith(".input") and not os.path.isabs(word):
            word = os.path.join(ctx["root"], word)
        args.append(word)
    cmd = [game, "--data", data, "--save", save, "--headless", "--offscreen", "--window", ctx["config"]["retail"]["window"]] + args
    if seed:
        cmd += ["--seed", str(seed)]
    cmd += ["--screenshot", shot]
    env = {"DC2_AUDIO": "off", "SDL_VIDEO_DRIVER": "offscreen"}
    icd = ctx["env"].get("CT_LAVAPIPE_ICD", "")
    if os.path.exists(icd):
        env["VK_ICD_FILENAMES"] = icd
        env["VK_DRIVER_FILES"] = icd
    result = util.run(cmd, log=os.path.join(out, name + ".log"), cwd=ctx["root"], env=env,
                      timeout=ctx["config"]["retail"]["process_timeout"])
    check = images.sanity(shot) if os.path.isfile(shot) else {"ok": False, "reason": "no screenshot written"}
    result["screenshot"] = shot
    result["sanity"] = check
    result["ok"] = result["exit"] == 0 and check["ok"]
    result["save"] = save
    return result


def _retail_ready(ctx, name):
    # No data is the usual case (no disc on the machine): not run, whatever the build. With data, a missing
    # game binary is an error.
    data, reason = _data_root(ctx)
    if not data:
        return report.stage(name, "G5", "not_run", reason), None
    if not _game_binary(ctx):
        return report.stage(name, "G5", "error", "no darkchronicle in %s: build first" % ctx["build_dir"]), None
    return None, data


def _smoke(ctx, name):
    for smoke in ctx["config"]["retail"]["smokes"]:
        if smoke["name"] == name:
            return smoke
    return None


def stage_retail_title(ctx):
    blocked, data = _retail_ready(ctx, "retail-title")
    if blocked:
        return blocked
    out = _stage_dir(ctx, "retail-title")
    result = _run_smoke(ctx, out, _smoke(ctx, "title"), data)
    summary = "title in %d frames: exit %d, screenshot %s" % (120, result["exit"], "sane %dx%d" % (
        result["sanity"].get("width", 0), result["sanity"].get("height", 0)) if result["sanity"]["ok"] else result["sanity"]["reason"])
    return report.stage("retail-title", "G5", "pass" if result["ok"] else "fail", summary, result["duration"], result["command"],
                        result["log"], [result["log"], result["screenshot"]], details={"sanity": result["sanity"]})


def stage_retail_smokes(ctx):
    blocked, data = _retail_ready(ctx, "retail-smokes")
    if blocked:
        return blocked
    out = _stage_dir(ctx, "retail-smokes")
    failed = []
    evidence = []
    commands = []
    total = 0.0
    details = {}
    for smoke in ctx["config"]["retail"]["smokes"]:
        result = _run_smoke(ctx, out, smoke, data, seed=ctx["config"]["retail"]["seed"])
        total += result["duration"]
        evidence += [result["log"], result["screenshot"]]
        commands.append(result["command"])
        details[smoke["name"]] = {"exit": result["exit"], "sanity": result["sanity"]}
        if not result["ok"]:
            failed.append("%s (exit %d, %s)" % (smoke["name"], result["exit"], result["sanity"].get("reason") or "screenshot ok"))
    summary = "%d smokes, %d failed" % (len(ctx["config"]["retail"]["smokes"]), len(failed)) + ("" if not failed else ": " + "; ".join(failed))
    return report.stage("retail-smokes", "G5", "fail" if failed else "pass", summary, total, " ; ".join(commands), None, evidence,
                        details=details)


def _markers(log, prefixes, own=None):
    """The marker lines of a log; each path in own (name -> path, the run's own files) reads as <name>.

    A run's own paths differ between two otherwise identical runs ("mglib: frame 120 written to .../run-a.png"),
    so they are replaced before the lists are compared."""
    replacements = sorted(((path, "<%s>" % name) for name, path in (own or {}).items() if path),
                          key=lambda item: -len(item[0]))
    found = []
    try:
        with open(log, encoding="utf-8", errors="replace") as f:
            for line in f:
                for prefix in prefixes:
                    index = line.find(prefix)
                    if index >= 0:
                        marker = line[index:].strip()
                        for path, placeholder in replacements:
                            marker = marker.replace(path, placeholder)
                        found.append(marker)
                        break
    except OSError:
        pass
    return found


def stage_retail_determinism(ctx):
    blocked, data = _retail_ready(ctx, "retail-determinism")
    if blocked:
        return blocked
    out = _stage_dir(ctx, "retail-determinism")
    smoke = _smoke(ctx, ctx["config"]["retail"]["determinism_smoke"])
    prefixes = ctx["config"]["retail"]["marker_prefixes"]
    runs = [_run_smoke(ctx, out, smoke, data, label="run-%s" % tag, seed=ctx["config"]["retail"]["seed"]) for tag in ("a", "b")]
    total = sum(r["duration"] for r in runs)
    evidence = [p for r in runs for p in (r["log"], r["screenshot"])]
    markers = []
    for r, tag in zip(runs, ("a", "b")):
        found = _markers(r["log"], prefixes, {"screenshot": r.get("screenshot"), "save": r.get("save")})
        path = os.path.join(out, "markers-%s.txt" % tag)
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(found) + "\n")
        evidence.append(path)
        markers.append(found)
    commands = " ; ".join(r["command"] for r in runs)
    if not all(r["ok"] for r in runs):
        return report.stage("retail-determinism", "G5", "fail", "a run failed: exits %s" % [r["exit"] for r in runs], total,
                            commands, runs[0]["log"], evidence)
    shots = images.compare(runs[0]["screenshot"], runs[1]["screenshot"])
    same_markers = markers[0] == markers[1]
    enough = len(markers[0]) >= ctx["config"]["retail"]["min_markers"]
    ok = same_markers and enough and shots.get("identical", False)
    summary = "%d stable markers %s; screenshots %s (mean %s, p99.5 %s)" % (
        len(markers[0]), "identical" if same_markers else "DIFFER", "identical" if shots.get("identical") else "differ",
        shots.get("mean"), shots.get("p99.5"))
    if not enough:
        summary += "; fewer than %d markers (add prefixes to stages.json retail.marker_prefixes)" % ctx["config"]["retail"]["min_markers"]
    return report.stage("retail-determinism", "G5", "pass" if ok else "fail", summary, total, commands, runs[0]["log"], evidence,
                        details={"markers": len(markers[0]), "same_markers": same_markers, "screenshots": shots})


def stage_retail_golden(ctx):
    blocked, data = _retail_ready(ctx, "retail-golden")
    if blocked:
        return blocked
    golden = ctx["golden_dir"]
    shots = os.path.join(ctx["report_dir"], "retail-smokes")
    current = {s["name"]: os.path.join(shots, s["name"] + ".png") for s in ctx["config"]["retail"]["smokes"]}
    current = {k: v for k, v in current.items() if os.path.isfile(v)}
    if not current:
        return report.stage("retail-golden", "G5", "not_run", "no screenshots from retail-smokes in this run")
    if ctx["options"]["approve_golden"]:
        os.makedirs(golden, exist_ok=True)
        for name, path in current.items():
            shutil.copy2(path, os.path.join(golden, name + ".png"))
        util.write_json(os.path.join(golden, "approved.json"), {"approved": sorted(current), "head": ctx["repos"].get("ChronicleTwo", {}).get("head"),
                                                                 "created": report.new_report([], {}, {})["created"]})
    approved = {name: os.path.join(golden, name + ".png") for name in current if os.path.isfile(os.path.join(golden, name + ".png"))}
    if not approved:
        return report.stage("retail-golden", "G5", "not_run", "no approved golden set in %s (run.py --approve-golden after looking at the shots)" % golden)
    started = time.monotonic()
    results = {}
    failed = []
    for name, path in approved.items():
        results[name] = images.compare(current[name], path)
        if not results[name]["pass"]:
            failed.append("%s (mean %s, p99.5 %s)" % (name, results[name]["mean"], results[name]["p99.5"]))
    util.write_json(os.path.join(_stage_dir(ctx, "retail-golden"), "check.json"), results)
    summary = "%d frame(s) against %s: mean <= %.1f and p99.5 <= %d; %d failed" % (len(approved), golden, images.MEAN_LIMIT, images.P995_LIMIT, len(failed)) + (
        "" if not failed else ": " + "; ".join(failed))
    return report.stage("retail-golden", "G5", "fail" if failed else "pass", summary, time.monotonic() - started,
                        "python3 -I tools/gauntlet/run.py --stage retail-smokes --stage retail-golden", None,
                        [os.path.join(ctx["report_dir"], "retail-golden", "check.json")] + list(current.values()), details=results)


STAGES = [
    ("env", "G0", stage_env),
    ("build", "G0", stage_build),
    ("ctest", "G0", stage_ctest),
    ("capacity", "G1", stage_capacity),
    ("toolkit", "G1", stage_toolkit),
    ("toolkit-limits", "G1", stage_toolkit_limits),
    ("content", "G2", stage_content),
    ("lua-harness", "G2", stage_lua_harness),
    ("web-node", "G3", stage_web_node),
    ("web-browser", "G3", stage_web_browser),
    ("coop", "G4", stage_coop),
    ("retail-title", "G5", stage_retail_title),
    ("retail-smokes", "G5", stage_retail_smokes),
    ("retail-determinism", "G5", stage_retail_determinism),
    ("retail-golden", "G5", stage_retail_golden),
]

GATES = ["G0", "G1", "G2", "G3", "G4", "G5"]


def select(gates=None, names=None):
    chosen = []
    for name, gate, func in STAGES:
        if names and name in names or gates and gate in gates or not names and not gates:
            chosen.append((name, gate, func))
    return chosen


def reorder(chosen, last):
    """chosen with the stages named in last moved to the end, in the order given (--last): a disk-hungry
    stage such as the toolkit's cargo build runs after the rest, and its target can be removed right after."""
    if not last:
        return chosen
    head = [c for c in chosen if c[0] not in last]
    tail = [c for name in last for c in chosen if c[0] == name]
    return head + tail
