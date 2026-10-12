#!/usr/bin/env python3
"""Prepare checked host-only edits on a mirror of upstream PS2 translation units.

    upstream_host_fixups.py <ps2_src_dir> <mirror_dir> <stamp_file>

The patch records host compilation adaptations. It is applied to generated copies;
retail sources remain byte-for-byte upstream. Every hunk is checked against the source
mirror before any patch is written.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

PATCH = Path(__file__).with_name("upstream_host_fixups.patch")


def prepare(src: Path, dst: Path) -> None:
    if not src.is_dir():
        sys.exit(f"upstream_host_fixups: source directory does not exist: {src}")
    src = src.resolve()
    dst = dst.resolve()
    if dst == src or src in dst.parents:
        sys.exit("upstream_host_fixups: mirror must not be the source directory or inside it")
    dst.mkdir(parents=True, exist_ok=True)
    expected = set()
    for item in src.iterdir():
        target = dst / item.name
        if item.is_dir():
            shutil.copytree(item, target, dirs_exist_ok=True)
            expected.update(p.relative_to(dst) for p in target.rglob("*"))
        elif item.is_file():
            shutil.copy2(item, target)
            expected.add(target.relative_to(dst))

    for path in sorted((p for p in dst.rglob("*") if p.is_file()), reverse=True):
        if path.relative_to(dst) not in expected:
            path.unlink()
    for path in sorted((p for p in dst.rglob("*") if p.is_dir()), reverse=True):
        if not any(path.iterdir()):
            path.rmdir()

    # Patch paths are a/ps2/src/...; stripping those three components targets the
    # mirror root, whose contents are the original src directory.
    # Repository discovery would silently skip these paths in an in-tree build.
    patch_env = os.environ.copy()
    for name in ("GIT_DIR", "GIT_WORK_TREE", "GIT_COMMON_DIR", "GIT_INDEX_FILE", "GIT_PREFIX"):
        patch_env.pop(name, None)
    patch_env["GIT_CEILING_DIRECTORIES"] = str(dst.parent)
    check = subprocess.run(
        ["git", "-c", "core.autocrlf=false", "apply", "--check", "-p3", str(PATCH.resolve())], cwd=dst,
        env=patch_env, text=True, capture_output=True,
    )
    if check.returncode:
        sys.exit(f"upstream_host_fixups: patch does not match source mirror:\n{check.stdout}{check.stderr}")
    apply = subprocess.run(
        ["git", "-c", "core.autocrlf=false", "apply", "-p3", str(PATCH.resolve())], cwd=dst,
        env=patch_env, text=True, capture_output=True,
    )
    if apply.returncode:
        sys.exit(f"upstream_host_fixups: patch application failed:\n{apply.stdout}{apply.stderr}")

    # The port's MainLoop replacement shares these retail translation-unit globals and callbacks.
    # Make just those names externally linkable in the generated host copy; the original PS2 sources stay untouched.
    mainloop = dst / "mainloop.cpp"
    text = mainloop.read_bytes().decode("latin-1")
    for declaration in (
        "static int NextLoopNo;",
        "static int LoopNo;",
        "static int PrevLoopNo;",
        "static int CaptureScreen;",
        "static u_long128 SystemSeBuff[400];",
        "static u_long128 InfoBuff[5000];",
        "static CSaveData *ActiveSaveData;",
        "static int CaptureMode;",
        "static int PlayTimeCountFlag;",
        "static CSubGameData *SubGameSaveData;",
        "static void InitPadTable(int language) {",
        "static void VSyncCallBack(int unused) {",
    ):
        if text.count(declaration) != 1:
            sys.exit(f"upstream_host_fixups: mainloop.cpp must contain {declaration!r} exactly once")
        text = text.replace(declaration, declaration.removeprefix("static "), 1)
    mainloop.write_bytes(text.encode("latin-1"))

    # MWCC permits this non-trivial member in an anonymous union; clang deletes
    # its constructor. Preserve the same unconstructed storage in the host copy.
    menu = dst / "menucls1.cpp"
    text = menu.read_bytes().decode("latin-1")
    old = "    union {\n        CItemUseTarget target_data;\n    };\n"
    new = ("    alignas(CItemUseTarget) u_char target_data_storage[sizeof(CItemUseTarget)];\n"
           "    CItemUseTarget &target_data = *reinterpret_cast<CItemUseTarget *>(target_data_storage);\n")
    if text.count(old) != 1:
        sys.exit("upstream_host_fixups: menucls1.cpp anonymous union must occur exactly once")
    menu.write_bytes(text.replace(old, new).encode("latin-1"))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("src_dir", type=Path)
    parser.add_argument("dst_dir", type=Path)
    parser.add_argument("stamp_file", type=Path)
    args = parser.parse_args()
    prepare(args.src_dir, args.dst_dir)
    args.stamp_file.parent.mkdir(parents=True, exist_ok=True)
    args.stamp_file.touch()


if __name__ == "__main__":
    main()
