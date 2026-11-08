"""The gauntlet report: report.json, report.md and the latest symlink.

Schema (gauntlet_version 1):
  gauntlet_version, created, host {hostname, os, machine, cpus, memory_gb, python},
  repos {name: {path, head, branch, dirty}}, arguments, options,
  stages [{name, gate, status (pass|fail|not_run|error), duration_s, summary, command, log,
           evidence [paths], baseline {new, fixed, known} or null, details {}}],
  verdict (pass|fail), exit_code.
Exit code 0 only when every stage that ran passed and no stage errored.
"""
import datetime
import os
import platform
import socket

from . import VERSION

STATUSES = ("pass", "fail", "not_run", "error")


def host_info():
    memory_gb = None
    try:
        with open("/proc/meminfo") as f:
            for line in f:
                if line.startswith("MemTotal:"):
                    memory_gb = round(int(line.split()[1]) / 1048576, 1)
    except OSError:
        pass
    return {"hostname": socket.gethostname(), "os": platform.platform(), "machine": platform.machine(),
            "cpus": os.cpu_count(), "memory_gb": memory_gb, "python": platform.python_version()}


def new_report(arguments, options, repos):
    return {"gauntlet_version": VERSION,
            "created": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
            "host": host_info(), "repos": repos, "arguments": list(arguments), "options": dict(options),
            "stages": [], "verdict": "pass", "exit_code": 0}


def stage(name, gate, status, summary, duration=0.0, command=None, log=None, evidence=None, baseline=None,
          details=None):
    if status not in STATUSES:
        raise ValueError("bad status %r" % status)
    return {"name": name, "gate": gate, "status": status, "duration_s": round(float(duration), 2),
            "summary": summary, "command": command, "log": log, "evidence": list(evidence or []),
            "baseline": baseline, "details": details or {}}


def finish(report):
    ran = [s for s in report["stages"] if s["status"] in ("pass", "fail", "error")]
    bad = [s for s in ran if s["status"] != "pass"]
    report["verdict"] = "fail" if bad else "pass"
    report["exit_code"] = 1 if bad else 0
    return report


def exit_code(report):
    return finish(dict(report))["exit_code"]


def validate(report):
    errors = []
    for key in ("gauntlet_version", "created", "host", "repos", "arguments", "options", "stages", "verdict",
                "exit_code"):
        if key not in report:
            errors.append("missing " + key)
    for s in report.get("stages", []):
        for key in ("name", "gate", "status", "duration_s", "summary", "command", "log", "evidence", "baseline",
                    "details"):
            if key not in s:
                errors.append("stage %s missing %s" % (s.get("name"), key))
        if s.get("status") not in STATUSES:
            errors.append("stage %s bad status %r" % (s.get("name"), s.get("status")))
    ran_bad = any(s.get("status") in ("fail", "error") for s in report.get("stages", []))
    if ran_bad != (report.get("exit_code") == 1):
        errors.append("exit_code does not follow the stages")
    return errors


def render_md(report):
    lines = ["# Gauntlet report", "",
             "Created %s on %s (%s, %s CPUs, %s GB)." % (report["created"], report["host"]["hostname"],
                                                         report["host"]["os"], report["host"]["cpus"],
                                                         report["host"]["memory_gb"]),
             "Verdict: **%s** (exit %d)." % (report["verdict"].upper(), report["exit_code"]), "",
             "Arguments: `%s`" % " ".join(report["arguments"]), "", "## Repositories", "",
             "| Repository | Head | Branch | Dirty |", "|---|---|---|---|"]
    for name, info in report["repos"].items():
        if not info.get("present"):
            lines.append("| %s | absent | | |" % name)
        elif not info.get("git"):
            lines.append("| %s | not a git checkout | | |" % name)
        else:
            lines.append("| %s | `%s` | %s | %s |" % (name, info["head"][:12], info.get("branch"),
                                                    "yes" if info.get("dirty") else "no"))
    lines += ["", "## Stages", "", "| Gate | Stage | Status | Time | Summary |", "|---|---|---|---|---|"]
    for s in report["stages"]:
        lines.append("| %s | %s | %s | %.1fs | %s |" % (s["gate"], s["name"], s["status"], s["duration_s"],
                                                       s["summary"].replace("|", "/").replace("\n", " ")))
    for s in report["stages"]:
        if s["status"] == "not_run" and not s["command"] and not s["evidence"]:
            continue
        lines += ["", "### %s %s: %s" % (s["gate"], s["name"], s["status"]), "", s["summary"]]
        if s["command"]:
            lines += ["", "Reproduce:", "", "```", s["command"], "```"]
        if s["log"]:
            lines += ["", "Log: `%s`" % s["log"]]
        if s["evidence"]:
            lines += ["", "Evidence:", ""] + ["- `%s`" % e for e in s["evidence"]]
        b = s.get("baseline")
        if b:
            lines += ["", "Against the baseline: %d new failure(s), %d fixed, %d known." % (
                len(b.get("new", [])), len(b.get("fixed", [])), len(b.get("known", [])))]
            if b.get("new"):
                lines += ["", "New failures:", ""] + ["- `%s`" % n for n in b["new"]]
            if b.get("fixed"):
                lines += ["", "Fixed since the baseline:", ""] + ["- `%s`" % n for n in b["fixed"]]
    return "\n".join(lines) + "\n"


def write(report, directory):
    import json
    finish(report)
    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, "report.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1)
        f.write("\n")
    with open(os.path.join(directory, "report.md"), "w", encoding="utf-8") as f:
        f.write(render_md(report))
    latest = os.path.join(os.path.dirname(directory.rstrip("/")), "latest")
    try:
        if os.path.islink(latest) or os.path.exists(latest):
            os.remove(latest)
        os.symlink(os.path.basename(directory.rstrip("/")), latest)
    except OSError:
        pass
    return path
