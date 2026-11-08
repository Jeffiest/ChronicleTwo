"""Known failures: the verdict of a test stage is "no new failures", not "all green".

A baseline file (tools/gauntlet/baselines/<platform>.json) holds, per stage key, the names of the
cases that failed when it was written, with the totals and the git head of that run. diff() splits
the current failures into new (fail the gate), known, and fixed (in the baseline, passed now).
"""
import datetime
import platform
import sys

from . import util


def platform_name():
    system = sys.platform
    if system.startswith("linux"):
        system = "linux"
    return "%s-%s" % (system, platform.machine())


def load(path):
    return util.read_json(path, default={}) or {}


def entry(data, key):
    return (data or {}).get(key) or {}


def diff(current_failures, ran, data, key):
    known_failures = set(entry(data, key).get("failures", []))
    current = set(current_failures)
    ran = set(ran) | current
    return {"new": sorted(current - known_failures),
            "known": sorted(current & known_failures),
            "fixed": sorted(name for name in known_failures if name in ran and name not in current),
            "unseen": sorted(name for name in known_failures if name not in ran),
            "has_baseline": key in (data or {})}


def update(path, key, failures, totals, head=None, note=None):
    data = load(path)
    data[key] = {"updated": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
                 "head": head, "note": note, "totals": totals, "failures": sorted(set(failures))}
    data.setdefault("platform", platform_name())
    util.write_json(path, data)
    return data
