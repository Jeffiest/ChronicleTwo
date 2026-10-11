#!/usr/bin/env python3
"""Mirror ps2/include into the build with the header fixups the port compiles against.

    header_fixups.py <src_dir> <dst_dir> <stamp_file>

Every fixup's old text must occur exactly once in its file; anything else fails before a byte is written.
"""

import argparse
import sys
from pathlib import Path

# MWCC accepts a `void` override of a base's `int` virtual and gives it a new vtable slot; clang rejects it.
# The PS2 vtables keep those separate slots, so making them overrides here is an accepted difference until
# rendering replaces these draws.
FIXUPS = [
    ("dng_event.hpp", "    virtual void Draw();", "    virtual int Draw();"),
    ("dng_hud.hpp", "    virtual void Draw();", "    virtual int Draw();"),
]


def build_mirror(src: Path) -> dict[str, bytes]:
    files = {}
    for path in sorted(p for p in src.rglob("*") if p.is_file()):
        files[path.relative_to(src).as_posix()] = path.read_bytes()

    for rel, old, new in FIXUPS:
        if rel not in files:
            sys.exit(f"header_fixups: {rel} is not under {src}; its fixup has no target")
        data = files[rel]
        count = data.count(old.encode())
        if count != 1:
            sys.exit(f"header_fixups: {rel} has {count} occurrences of {old!r}, expected exactly 1")
        files[rel] = data.replace(old.encode(), new.encode())
    return files


def write_mirror(dst: Path, files: dict[str, bytes]) -> None:
    dst.mkdir(parents=True, exist_ok=True)
    for rel, data in files.items():
        out = dst / rel
        if out.is_file() and out.read_bytes() == data:
            continue
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(data)

    for path in sorted((p for p in dst.rglob("*") if p.is_file()), reverse=True):
        if path.relative_to(dst).as_posix() not in files:
            path.unlink()
    for path in sorted((p for p in dst.rglob("*") if p.is_dir()), reverse=True):
        if not any(path.iterdir()):
            path.rmdir()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("src_dir", type=Path)
    parser.add_argument("dst_dir", type=Path)
    parser.add_argument("stamp_file", type=Path)
    args = parser.parse_args()

    if not args.src_dir.is_dir():
        sys.exit(f"header_fixups: {args.src_dir} is not a directory")

    write_mirror(args.dst_dir, build_mirror(args.src_dir))
    args.stamp_file.parent.mkdir(parents=True, exist_ok=True)
    args.stamp_file.touch()


if __name__ == "__main__":
    main()
