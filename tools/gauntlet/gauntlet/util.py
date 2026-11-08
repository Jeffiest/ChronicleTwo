"""Processes, paths and git for the gauntlet.

run(cmd, log, ...) runs one command with a timeout and writes its output to a log file;
read_env_sh(path) loads tools/gauntlet/env.sh so the runner and a person use the same flags;
free_port() picks a free TCP port; git_head(path) describes a repository for the report.
"""
import json
import os
import shutil
import socket
import subprocess
import sys
import time


def run(cmd, log=None, cwd=None, env=None, timeout=600, stdin=None):
    started = time.monotonic()
    merged = dict(os.environ)
    if env:
        merged.update({k: str(v) for k, v in env.items()})
    text = ""
    timed_out = False
    try:
        done = subprocess.run(cmd, cwd=cwd, env=merged, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              timeout=timeout, stdin=stdin or subprocess.DEVNULL)
        code = done.returncode
        text = done.stdout.decode("utf-8", "replace")
    except subprocess.TimeoutExpired as e:
        code = -1
        timed_out = True
        text = (e.stdout or b"").decode("utf-8", "replace") + "\n[gauntlet] timed out after %ss\n" % timeout
    except FileNotFoundError as e:
        code = 127
        text = "[gauntlet] %s\n" % e
    duration = time.monotonic() - started
    if log:
        os.makedirs(os.path.dirname(log), exist_ok=True)
        with open(log, "w", encoding="utf-8") as f:
            f.write("$ " + shell_join(cmd) + "\n")
            if cwd:
                f.write("# cwd " + str(cwd) + "\n")
            if env:
                f.write("# env " + " ".join("%s=%s" % kv for kv in sorted(env.items())) + "\n")
            f.write(text)
            f.write("\n[gauntlet] exit %d after %.1fs\n" % (code, duration))
    return {"exit": code, "timed_out": timed_out, "duration": round(duration, 2), "output": text,
            "command": shell_join(cmd), "log": log}


def shell_join(cmd):
    out = []
    for word in cmd:
        word = str(word)
        if word == "" or any(c in word for c in " \t\n'\"$&|;<>()*?[]{}\\"):
            word = "'" + word.replace("'", "'\\''") + "'"
        out.append(word)
    return " ".join(out)


def which(*names):
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    return None


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def wait_for(predicate, timeout=30.0, step=0.2):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(step)
    return predicate()


def git(path, *args):
    try:
        out = subprocess.run(["git", "-C", path, *args], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                             timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if out.returncode != 0:
        return None
    return out.stdout.decode("utf-8", "replace").strip()


def git_head(path):
    if not os.path.isdir(path):
        return {"path": path, "present": False}
    head = git(path, "rev-parse", "HEAD")
    if head is None:
        return {"path": path, "present": True, "git": False}
    status = git(path, "status", "--porcelain", "--untracked-files=no") or ""
    return {"path": path, "present": True, "git": True, "head": head,
            "branch": git(path, "rev-parse", "--abbrev-ref", "HEAD"),
            "dirty": bool(status.strip())}


def read_env_sh(path):
    script = ('set -a; . "$1" >/dev/null 2>&1; "$2" -I -c '
              '"import json, os; print(json.dumps({k: v for k, v in os.environ.items() '
              'if k.startswith(\'CT_\') or k in (\'EM_CONFIG\', \'CXXFLAGS\', \'CXXSTDLIB\')}))"')
    out = subprocess.run(["bash", "-c", script, "_", path, sys.executable], stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, timeout=60)
    if out.returncode != 0:
        raise RuntimeError("cannot read %s: %s" % (path, out.stderr.decode("utf-8", "replace")))
    env = json.loads(out.stdout.decode("utf-8"))
    env["CMAKE_FLAGS"] = [line for line in env.get("CT_CMAKE_FLAGS", "").splitlines() if line.strip()]
    return env


def disk_free_gb(path):
    """Free space in GB on the filesystem holding path (its nearest existing parent), or None."""
    path = os.path.abspath(path or ".")
    while path and not os.path.exists(path):
        parent = os.path.dirname(path)
        if parent == path:
            break
        path = parent
    try:
        st = os.statvfs(path)
    except OSError:
        return None
    return st.f_bavail * st.f_frsize / 1e9


def file_size(path):
    try:
        return os.path.getsize(path)
    except OSError:
        return -1


def write_json(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1, sort_keys=False)
        f.write("\n")


def read_json(path, default=None):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return default


def tail(text, lines=20):
    parts = text.rstrip().splitlines()
    return "\n".join(parts[-lines:])
