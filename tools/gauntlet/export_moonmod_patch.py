"""Regenerate MoonMod's ports/chronicle-two patches from this tree.

  python3 -I tools/gauntlet/export_moonmod_patch.py OUT.patch
      git diff <upstream pin> -- port/ scripts/ CMakeLists.txt CMakePresets.json docs/, without tools/kazarov
      and port/web: what apply-moonfactory.ps1 applies to a clean ChronicleTwo at the pin with Kazarov as
      the submodule it pins. (.gitmodules and DC2_PLAN.md differ too and are left out on purpose: MoonMod's
      checkout keeps Kazarov as a submodule, and the plan is this tree's.)
  python3 -I tools/gauntlet/export_moonmod_patch.py --kazarov OUT.patch
      the diff of tools/kazarov against upstream Kazarov at its pin (fd72338a), cloned once into
      tools/gauntlet/.cache/kazarov-upstream with GIT_LFS_SKIP_SMUDGE=1 (archive downloads are blocked
      here; git clones work).
  --check             also `git apply --check` the written patch in a throwaway worktree at the pin: for the
                      tree patch in an upstream ChronicleTwo clone (--upstream DIR, $CHRONICLETWO_UPSTREAM,
                      ../themoonpeople/chronicletwo, else a clone made in tools/gauntlet/.cache, else this
                      repository's own history), for --kazarov in the Kazarov clone.
  --committed         diff the pin against HEAD, not the working tree: what is committed, without edits in
                      flight (use it for what goes to MoonMod). Without it the working tree is diffed and
                      the script warns about uncommitted edits and untracked files under the paths, which
                      the patch carries and lacks respectively.
  --pin SHA           another upstream pin (default: the one apply-moonfactory.ps1 names, 7a7d7f29...)
  --kazarov-pin SHA   another Kazarov pin (default fd72338a...)

Exit 0 when the patch was written (and, with --check, applies); 1 otherwise.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
UPSTREAM_PIN = "7a7d7f29ae544a04e65634278d244b785d801066"
KAZAROV_PIN = "fd72338a6aebf5c119947bb3150bba580217fec9"
UPSTREAM_URL = "https://github.com/TheMoonPeople/chronicletwo"
KAZAROV_URL = "https://github.com/TheMoonPeople/Kazarov"
PATHS = ["port/", "scripts/", "CMakeLists.txt", "CMakePresets.json", "docs/"]
EXCLUDE = [":(exclude)tools/kazarov", ":(exclude)port/web", ":(exclude)port/build"]


def git(cwd, *args, check=True, env=None):
    merged = dict(os.environ)
    merged.update(env or {})
    out = subprocess.run(["git", "-C", cwd, *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=merged)
    if check and out.returncode != 0:
        raise SystemExit("git %s failed:\n%s" % (" ".join(args), out.stderr.decode("utf-8", "replace")))
    return out


def has_commit(repo, sha):
    return os.path.isdir(repo) and git(repo, "cat-file", "-e", sha + "^{commit}", check=False).returncode == 0


def working_tree_notes():
    """Uncommitted edits and untracked files under the exported paths: a working-tree patch carries the
    first and silently lacks the second (git diff does not see untracked files)."""
    modified = git(ROOT, "diff", "--name-only", "HEAD", "--", *PATHS, *EXCLUDE).stdout.decode().split()
    untracked = git(ROOT, "ls-files", "--others", "--exclude-standard", "--", *PATHS, *EXCLUDE).stdout.decode().split()
    return modified, untracked


def export_tree(pin, out_path, committed=False):
    git(ROOT, "cat-file", "-e", pin + "^{commit}")
    revs = [pin, "HEAD"] if committed else [pin]
    if not committed:
        modified, untracked = working_tree_notes()
        if modified:
            print("warning: %d file(s) under the paths have uncommitted edits, which the patch carries (--committed leaves "
                  "them out): %s" % (len(modified), ", ".join(modified[:8]) + (" ..." if len(modified) > 8 else "")))
        if untracked:
            print("warning: %d untracked file(s) under the paths are NOT in the patch: %s" % (
                len(untracked), ", ".join(untracked[:8]) + (" ..." if len(untracked) > 8 else "")))
    diff = git(ROOT, "diff", "--no-color", "--no-ext-diff", "--binary", *revs, "--", *PATHS, *EXCLUDE)
    text = diff.stdout
    if not text.strip():
        print("the tree does not differ from %s under %s" % (pin[:8], " ".join(PATHS)))
    with open(out_path, "wb") as f:
        f.write(text)
    stat = git(ROOT, "diff", "--stat=120", *revs, "--", *PATHS, *EXCLUDE).stdout.decode("utf-8", "replace")
    print(stat.rstrip().splitlines()[-1] if stat.strip() else "0 files changed")
    return text


def clone_pinned(url, clone, pin):
    """A --no-checkout clone holding the pin (fetched by SHA when the clone is shallow or old)."""
    env = {"GIT_LFS_SKIP_SMUDGE": "1"}
    if not os.path.isdir(os.path.join(clone, ".git")):
        os.makedirs(os.path.dirname(clone), exist_ok=True)
        out = subprocess.run(["git", "clone", "--no-checkout", url, clone], env=dict(os.environ, **env),
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if out.returncode != 0:
            raise SystemExit("cloning %s failed:\n%s" % (url, out.stderr.decode("utf-8", "replace")))
    if not has_commit(clone, pin):
        git(clone, "fetch", "origin", pin, env=env)
    return clone


def kazarov_upstream(pin):
    return clone_pinned(KAZAROV_URL, os.path.join(HERE, ".cache", "kazarov-upstream"), pin)


def chronicletwo_upstream(pin, explicit=None):
    """Where the tree patch is checked: an upstream clone that has the pin, made in .cache when none is around."""
    candidates = [explicit, os.environ.get("CHRONICLETWO_UPSTREAM"),
                  os.path.join(os.path.dirname(ROOT), "themoonpeople", "chronicletwo")]
    for path in candidates:
        if path and has_commit(path, pin):
            return path, "upstream clone"
    cache = os.path.join(HERE, ".cache", "chronicletwo-upstream")
    try:
        return clone_pinned(UPSTREAM_URL, cache, pin), "upstream clone"
    except SystemExit as e:
        print("note: %s; checking against this repository's own history instead" % str(e).splitlines()[0])
    return ROOT, "this repository"


def export_kazarov(pin, out_path):
    clone = kazarov_upstream(pin)
    vendored = os.path.join(ROOT, "tools", "kazarov")
    with tempfile.TemporaryDirectory() as tmp:
        upstream = os.path.join(tmp, "upstream")
        os.makedirs(upstream)
        archive = git(clone, "archive", "--format=tar", pin)
        subprocess.run(["tar", "-x", "-C", upstream], input=archive.stdout, check=True)
        ours = os.path.join(tmp, "ours")
        shutil.copytree(vendored, ours, ignore=lambda d, names: [n for n in names if n == ".git" or (d == vendored and n in ("target", "build"))], symlinks=True)
        diff = subprocess.run(["git", "diff", "--no-index", "--no-color", "--binary", "--src-prefix=a/", "--dst-prefix=b/",
                               "upstream", "ours"], cwd=tmp, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    text = diff.stdout
    for side in ("upstream", "ours"):
        for prefix in ("a", "b"):
            text = text.replace(("%s/%s/" % (prefix, side)).encode(), (prefix + "/").encode())
    with open(out_path, "wb") as f:
        f.write(text)
    files = sum(1 for line in text.splitlines() if line.startswith(b"diff --git "))
    print("%d file(s) differ from Kazarov %s" % (files, pin[:8]))
    return text


def check_applies(repo, pin, patch_path, label):
    """git apply --check of the patch in a throwaway worktree of repo at pin."""
    with tempfile.TemporaryDirectory(dir=os.path.join(HERE, ".cache")) as tmp:
        worktree = os.path.join(tmp, "pin")
        git(repo, "worktree", "add", "--detach", worktree, pin)
        try:
            out = git(worktree, "apply", "--check", "--whitespace=nowarn", patch_path, check=False)
        finally:
            git(repo, "worktree", "remove", "--force", worktree, check=False)
            git(repo, "worktree", "prune", check=False)
    if out.returncode != 0:
        print("the patch does not apply to %s %s:\n%s" % (label, pin[:8], out.stderr.decode("utf-8", "replace")[:2000]))
        return False
    print("applies cleanly to %s at %s (%s)" % (label, pin[:8], repo))
    return True


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out")
    ap.add_argument("--kazarov", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--committed", action="store_true", help="diff the pin against HEAD, not the working tree")
    ap.add_argument("--pin", default=UPSTREAM_PIN)
    ap.add_argument("--kazarov-pin", default=KAZAROV_PIN)
    ap.add_argument("--upstream", help="an upstream ChronicleTwo clone holding the pin, for --check")
    args = ap.parse_args(argv)
    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    os.makedirs(os.path.join(HERE, ".cache"), exist_ok=True)
    if args.kazarov:
        export_kazarov(args.kazarov_pin, out)
        print(out)
        if args.check and not check_applies(kazarov_upstream(args.kazarov_pin), args.kazarov_pin, out, "Kazarov"):
            return 1
        return 0
    export_tree(args.pin, out, committed=args.committed)
    print(out)
    if args.check:
        repo, label = chronicletwo_upstream(args.pin, args.upstream)
        if not check_applies(repo, args.pin, out, "ChronicleTwo " + label):
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
