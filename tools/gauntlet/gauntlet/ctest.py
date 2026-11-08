"""Run a ctest selection and read its JUnit results.

run(build_dir, ...) runs ctest with --output-junit (each gtest case is already its own process) and
returns the totals and the names of the failed and skipped cases.
"""
import os
import xml.etree.ElementTree as ET

from . import util


def parse_junit(path):
    result = {"total": 0, "passed": 0, "failed": 0, "notrun": 0, "skipped": 0, "failures": [], "cases": []}
    try:
        tree = ET.parse(path)
    except (OSError, ET.ParseError):
        return None
    for case in tree.getroot().iter("testcase"):
        name = case.get("name") or ""
        status = case.get("status") or ""
        failed = case.find("failure") is not None or case.find("error") is not None or status == "fail"
        skipped = case.find("skipped") is not None
        if skipped or status in ("notrun", "disabled"):
            result["notrun"] += 1
            result["skipped"] += 1
            outcome = "notrun"
        elif failed:
            result["failed"] += 1
            result["failures"].append(name)
            outcome = "fail"
        else:
            result["passed"] += 1
            outcome = "pass"
        result["total"] += 1
        result["cases"].append({"name": name, "status": outcome, "time": case.get("time")})
    return result


def run(build_dir, log, junit, regex=None, exclude_regex=None, label_exclude=None, label=None, jobs=1,
        env=None, timeout=3600, xvfb=False, extra=()):
    cmd = ["ctest", "--test-dir", build_dir, "--output-junit", junit, "--output-on-failure", "--parallel",
           str(jobs), "--no-tests=ignore", "--timeout", "600"]
    if regex:
        cmd += ["-R", regex]
    if exclude_regex:
        cmd += ["-E", exclude_regex]
    if label:
        cmd += ["-L", label]
    if label_exclude:
        cmd += ["-LE", label_exclude]
    cmd += list(extra)
    if xvfb and util.which("xvfb-run"):
        cmd = ["xvfb-run", "-a"] + cmd
    try:
        os.remove(junit)
    except OSError:
        pass
    result = util.run(cmd, log=log, cwd=build_dir, env=env, timeout=timeout)
    result["results"] = parse_junit(junit) if os.path.exists(junit) else None
    return result
