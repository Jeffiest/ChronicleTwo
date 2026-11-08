"""The Dark Cloud 2 engine gauntlet: build and check everything, gate by gate (GAUNTLET.md).

  python3 -I tools/gauntlet/run.py                      every gate in order; stops at the first red gate
  python3 -I tools/gauntlet/run.py --gate G0            one gate (repeatable)
  python3 -I tools/gauntlet/run.py --stage ctest        one stage (repeatable; --list names them)
  python3 -I tools/gauntlet/run.py --gate G0 --update-baseline   write the known-failure baseline
  python3 -I tools/gauntlet/run.py --json               print the report path only

Options: --clean (fresh configure and build), --no-build (test the binaries as built), --gpu (include the gpu-labelled tests on lavapipe under
xvfb-run), --keep-going (run later gates after a red one), --approve-golden (the smokes' screenshots
become the golden set), --data DIR / --iso FILE (retail data for G5), --web-build DIR, --build-dir DIR,
--jobs N, --timeout S (per check), --build-timeout S, --target T (build only T; repeatable),
--report-dir DIR, --coop-build DIR (package D's build for G4), --last STAGE (run that stage after the
others, e.g. the disk-hungry toolkit; repeatable), --min-free-gb N (the build and toolkit stages refuse to start below it; default 4), --min-free-gb-tests N
(the same for the ctest stages; default 1). Toolchain paths and cmake flags come from tools/gauntlet/env.sh.

The report goes to tools/gauntlet/report/<timestamp>/report.json and report.md, with a `latest`
symlink. Exit 0 only when every stage that ran passed and no stage errored.
"""
import argparse
import datetime
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from gauntlet import report, stages, util  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(HERE))


def parse(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gate", action="append", default=[], choices=stages.GATES, help="run this gate (repeatable)")
    ap.add_argument("--stage", action="append", default=[], help="run this stage (repeatable)")
    ap.add_argument("--list", action="store_true", help="list the stages and exit")
    ap.add_argument("--clean", action="store_true", help="remove the build directory first")
    ap.add_argument("--no-build", action="store_true", help="do not configure or build: test the binaries already in the build directory (reported as not_run with their link time)")
    ap.add_argument("--gpu", action="store_true", help="include gpu-labelled tests (lavapipe, xvfb-run)")
    ap.add_argument("--update-baseline", action="store_true", help="write the current failures as the baseline")
    ap.add_argument("--approve-golden", action="store_true", help="the retail smokes' screenshots become the golden set")
    ap.add_argument("--keep-going", action="store_true", help="run later gates even after a red one")
    ap.add_argument("--json", action="store_true", help="print only the report path")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--timeout", type=int, default=3600, help="seconds a check may take")
    ap.add_argument("--build-timeout", type=int, default=4 * 3600)
    ap.add_argument("--target", action="append", default=[], help="build only this target (repeatable)")
    ap.add_argument("--data", help="extracted retail data root (else DC2_DATA)")
    ap.add_argument("--iso", help="retail disc image (else DC2_ISO, rom/pal/*.iso)")
    ap.add_argument("--web-build", help="the web build directory (default port/build/web)")
    ap.add_argument("--build-dir", help="the PC build directory (default from env.sh)")
    ap.add_argument("--coop-build", help="the PC build G4 tests (package D's tree until it is merged; default the build directory)")
    ap.add_argument("--last", action="append", default=[], help="run this stage after the others (repeatable, in the order given)")
    ap.add_argument("--min-free-gb", type=float, default=4.0, help="the build and toolkit stages do not start with less free disk (0: no check)")
    ap.add_argument("--min-free-gb-tests", type=float, default=1.0, help="the ctest stages (capacity, content, coop) do not start with less free disk")
    ap.add_argument("--report-dir", help="where to write the report (default tools/gauntlet/report/<timestamp>)")
    ap.add_argument("--env-sh", default=os.path.join(HERE, "env.sh"))
    ap.add_argument("--stages-json", default=os.path.join(HERE, "stages.json"))
    return ap.parse_args(argv)


def context(options, argv):
    env = util.read_env_sh(options.env_sh)
    build_dir = options.build_dir or env.get("CT_BUILD_DIR") or os.path.join(ROOT, "port", "build", "pc")
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    report_dir = os.path.abspath(options.report_dir) if options.report_dir else os.path.join(HERE, "report", stamp)
    parent = os.path.dirname(ROOT)
    repos = {name: util.git_head(os.path.join(parent, name)) for name in ("ChronicleTwo", "DC2-Toolkit", "MoonMod", "DC_Web")}
    repos["ChronicleTwo"] = util.git_head(ROOT)
    from gauntlet import baseline as baseline_module
    return {"root": ROOT, "gauntlet_dir": HERE, "env_sh": options.env_sh, "env": env, "build_dir": build_dir,
            "report_dir": report_dir, "cache_dir": os.path.join(HERE, ".cache"), "golden_dir": os.path.join(HERE, "golden"),
            "baseline_path": os.path.join(HERE, "baselines", baseline_module.platform_name() + ".json"),
            "config": util.read_json(options.stages_json, default={}), "repos": repos, "argv": argv,
            "options": {"clean": options.clean, "no_build": options.no_build, "gpu": options.gpu, "update_baseline": options.update_baseline,
                        "approve_golden": options.approve_golden, "jobs": options.jobs, "timeout": options.timeout,
                        "build_timeout": options.build_timeout, "build_targets": options.target, "data": options.data,
                        "iso": options.iso, "web_build": options.web_build, "keep_going": options.keep_going,
                        "coop_build": options.coop_build, "min_free_gb": options.min_free_gb,
                        "min_free_gb_tests": options.min_free_gb_tests, "last": options.last}}


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    options = parse(argv)
    if options.list:
        for name, gate, _ in stages.STAGES:
            print("%s  %s" % (gate, name))
        return 0
    unknown = [n for n in options.stage + options.last if n not in {s[0] for s in stages.STAGES}]
    if unknown:
        print("unknown stage(s): %s (see --list)" % ", ".join(unknown), file=sys.stderr)
        return 2
    ctx = context(options, argv)
    os.makedirs(ctx["report_dir"], exist_ok=True)
    os.makedirs(ctx["cache_dir"], exist_ok=True)
    rep = report.new_report(argv, ctx["options"], ctx["repos"])
    rep["build_dir"] = ctx["build_dir"]
    rep["baseline"] = ctx["baseline_path"]
    chosen = stages.reorder(stages.select(options.gate or None, options.stage or None), options.last)
    red_gate = None
    for name, gate, func in chosen:
        if red_gate and gate != red_gate and not options.keep_going and gate > red_gate:
            result = report.stage(name, gate, "not_run", "gate %s is not green; %s does not start" % (red_gate, gate))
        else:
            if not options.json:
                print("[%s] %s ..." % (gate, name), flush=True)
            started = time.monotonic()
            try:
                result = func(ctx)
            except Exception as e:  # a bug in the stage itself is an error, never a pass
                import traceback
                result = report.stage(name, gate, "error", "stage raised %s: %s" % (type(e).__name__, e), time.monotonic() - started,
                                      details={"traceback": traceback.format_exc()})
            if result["status"] in ("fail", "error") and (red_gate is None or gate < red_gate):
                red_gate = gate
        rep["stages"].append(result)
        if not options.json:
            print("[%s] %s: %s  %s" % (gate, name, result["status"].upper(), result["summary"]), flush=True)
        report.write(rep, ctx["report_dir"])
    path = report.write(rep, ctx["report_dir"])
    if options.json:
        print(path)
    else:
        print("\n%s: %s (report %s)" % (rep["verdict"].upper(), ", ".join("%s %s" % (s["name"], s["status"]) for s in rep["stages"]), path))
    return rep["exit_code"]


if __name__ == "__main__":
    sys.exit(main())
