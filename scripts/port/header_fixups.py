#!/usr/bin/env python3
"""Mirror ps2/include into the build with the header fixups the port compiles against, and fix unit copies.

    header_fixups.py <src_dir> <dst_dir> <stamp_file>
    header_fixups.py --unit <copy_path>

Every fixup's old text must occur exactly once in its file; anything else fails before a byte is written.
Headers are then passed through long64, because the PS2's `long` is 64-bit and Windows' is not.
"""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from long64 import rewrite as rewrite_long64  # noqa: E402

LONG64_SUFFIXES = (".h", ".hpp", ".hh", ".inc", ".inl")

# MWCC accepts a `void` override of a base's `int` virtual and gives it a new vtable slot; clang rejects it.
# The PS2 vtables keep those separate slots, so making them overrides here is an accepted difference until
# rendering replaces these draws.
# COFF has no COMDAT-vs-strong resolution, so one out-of-line definition collides with every inline copy.
# ClsMes::Init is out-of-line only in dngmenu.cpp; making it inline everywhere leaves one definition.
# mgVu0FBOX is not trivially copyable, so memset on it trips -Wnontrivial-memcall.
FIXUPS = [
    ("dng_event.hpp", "    virtual void Draw();", "    virtual int Draw();"),
    ("dng_hud.hpp", "    virtual void Draw();", "    virtual int Draw();"),
    ("nd_meswin.hpp", "#ifndef MES_WINDOW_OUT_OF_LINE_INIT", "#if 1"),
    ("collision.hpp", "memset(&bbox, 0, sizeof(bbox));", "memset(static_cast<void *>(&bbox), 0, sizeof(bbox));"),
    # kernel32 exports these three names on Windows; the PS2 kernel's versions take their own link names so they never bind to kernel32.
    ("sce/eekernel.h", "int CreateThread(struct ThreadParam *param);", "int CreateThread(struct ThreadParam *param) __asm__(\"Ps2CreateThread\");"),
    ("sce/eekernel.h", "int TerminateThread(int thread_id);", "int TerminateThread(int thread_id) __asm__(\"Ps2TerminateThread\");"),
    ("sce/eekernel.h", "int GetThreadId(void);", "int GetThreadId(void) __asm__(\"Ps2GetThreadId\");"),
]

# Native disk-layout and declaration-order adaptations retained from the working port.
FIXUPS += [('mg_tanime.hpp',
  'template <>\n'
  'void mgRect<float>::Set(float new_left, float new_top, float new_right, float new_bottom);\n'
  '\n'
  'template <>\n'
  'inline mgRect<float>::mgRect() { Set(0, 0, 0, 0); }\n',
  '#ifdef PORT\n'
  'template <>\n'
  'void mgRect<float>::Set(float new_left, float new_top, float new_right, float new_bottom);\n'
  '#endif\n'
  '\n'
  'template <>\n'
  'inline mgRect<float>::mgRect() { Set(0, 0, 0, 0); }\n'
  '\n'
  '#ifndef PORT\n'
  'template <>\n'
  'void mgRect<float>::Set(float new_left, float new_top, float new_right, float new_bottom);\n'
  '#endif\n'),
 ('runscript.hpp',
  "    u32   addr;  /**< Nonnegative byte offset of the function's first instruction in the code "
  'section. */\n'
  '    char *name;  /**< Name of the function, for runtime error messages. */\n'
  '    int   local; /**< Number of frame slots, arguments included. */\n'
  '    int   arg;   /**< Number of argument slots. */\n',
  "    u32 addr; /**< Nonnegative byte offset of the function's first instruction in the code "
  'section. */\n'
  '#ifdef PORT\n'
  '    // The program images were built for the PS2 and never carry a host name pointer; the field '
  'is\n'
  '    // read as the file holds it so local and arg stay at their file offsets.\n'
  "    u32 name; /**< Name field as the file holds it; only the PS2 build's error messages read "
  'it. */\n'
  '#else\n'
  '    char *name; /**< Name of the function, for runtime error messages. */\n'
  '#endif\n'
  '    int local; /**< Number of frame slots, arguments included. */\n'
  '    int arg;   /**< Number of argument slots. */\n'),
 ('sce/sifdev.h',
  'struct sce_stat {',
  '#ifdef PORT\n'
  '/* glibc defines these as macros for struct stat; the SDK names its own fields. */\n'
  '#undef st_atime\n'
  '#undef st_mtime\n'
  '#undef st_ctime\n'
  '#endif\n'
  '\n'
  'struct sce_stat {')]

# The leading newline anchors each match to its line start: a copy already rewritten fails the exactly-once check
# instead of gaining a second `inline`.
UNIT_FIXUPS = [
    ("map.cpp", "\nCMapParts::CMapParts() { Initialize(); }", "\ninline CMapParts::CMapParts() { Initialize(); }"),
    ("dng_debug.cpp", "\nvoid CTreasureBox::Initialize() {", "\ninline void CTreasureBox::Initialize() {"),
]

# MWCC-only pragmas. ct_port builds with -Wall and no -Wno-unknown-pragmas, so they are blanked, not silenced.
MWCC_PRAGMA = re.compile(r"[ \t]*#[ \t]*pragma[ \t]+(?:cpp_extensions\b.*|push|pop)[ \t]*")


def neutralise_mwcc_pragmas(text: str) -> str:
    lines = text.split("\n")
    for i, line in enumerate(lines):
        body = line.rstrip("\r")
        if MWCC_PRAGMA.fullmatch(body):
            lines[i] = line[len(body):]
    return "\n".join(lines)


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

    for rel, data in files.items():
        if rel.endswith(LONG64_SUFFIXES):
            text = neutralise_mwcc_pragmas(data.decode("latin-1"))
            rewritten = rewrite_long64(text).encode("latin-1")
            if rewritten.count(b"\n") != data.count(b"\n"):
                sys.exit(f"header_fixups: {rel} changed its line count")
            files[rel] = rewritten
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


def fixup_unit(copy: Path) -> None:
    if not copy.is_file():
        sys.exit(f"header_fixups: {copy} is not a file")
    fixups = [(old, new) for name, old, new in UNIT_FIXUPS if name == copy.name]
    if not fixups:
        return

    data = copy.read_bytes()
    text = data.decode("latin-1")
    for old, new in fixups:
        count = text.count(old)
        if count != 1:
            sys.exit(f"header_fixups: {copy.name} has {count} occurrences of {old!r}, expected exactly 1")
        text = text.replace(old, new)

    fixed = text.encode("latin-1")
    if fixed != data:
        copy.write_bytes(fixed)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--unit", type=Path, metavar="COPY")
    parser.add_argument("src_dir", nargs="?", type=Path)
    parser.add_argument("dst_dir", nargs="?", type=Path)
    parser.add_argument("stamp_file", nargs="?", type=Path)
    args = parser.parse_args()

    if args.unit is not None:
        if any(arg is not None for arg in (args.src_dir, args.dst_dir, args.stamp_file)):
            parser.error("--unit takes no other arguments")
        fixup_unit(args.unit)
        return

    if any(arg is None for arg in (args.src_dir, args.dst_dir, args.stamp_file)):
        parser.error("expected <src_dir> <dst_dir> <stamp_file>, or --unit <copy_path>")
    if not args.src_dir.is_dir():
        sys.exit(f"header_fixups: {args.src_dir} is not a directory")

    write_mirror(args.dst_dir, build_mirror(args.src_dir))
    args.stamp_file.parent.mkdir(parents=True, exist_ok=True)
    args.stamp_file.touch()


if __name__ == "__main__":
    main()
