#!/usr/bin/env python3
"""Text rewrites for the Windows build of ps2/, which ps2/ itself cannot take.

The PS2's long is 64 bits and Windows' is 32, so each literal `long` or `unsigned long` that holds a
64-bit value (GS register words, MPEG time stamps, the costume mask) becomes s64 or u64 in the
copies the port compiles. The rest are definitions that ELF links as the strong one of an inline
pair and COFF reports as duplicates, and one function libc++ already declares.

    win_rewrite.py units
    win_rewrite.py unit ORIGINAL STRIPPED     rewrite a unit in place, after port_override.py stripped it
    win_rewrite.py headers SRC DST            mirror ps2/include into DST with the headers rewritten

Every rule states how many matches the original file has, and the run fails on any other count, so
a change to ps2/ that moves a site is noticed instead of silently left as it was. The count is
taken on the original: the strip may already have removed a definition the rule would have reached.
"""

import pathlib
import re
import sys

U64 = r"\bunsigned long\b(?! long)"


def blank(match):
    """Replaces a span with as many empty lines, so the copy keeps the original's line numbers."""
    return "\n" * match.group(0).count("\n")


RULES = {
    "src/crandom.cpp": [
        # libc++'s stdlib.h declares abs(float) in the global namespace; a second one conflicts.
        (r"float abs\(float value\) \{\n    return value < 0\.0f \? -value : value;\n\}\n", blank, 1),
    ],
    # The unit defines out of line what every other unit sees inline: ELF lets the strong one win,
    # COFF reports a duplicate. Left inline, the definitions agree.
    "src/dng_debug.cpp": [
        (r"^#define DNG_DEBUG_SOURCE$", "", 1),
        (r"^void CTreasureBox::Initialize\(\) \{\n(?:    .*\n)*?\}\n", blank, 1),
    ],
    "src/dngmenu.cpp": [
        (r"^#define MES_WINDOW_OUT_OF_LINE_INIT$", "", 1),
        (r"^#undef MES_WINDOW_OUT_OF_LINE_INIT$", "", 1),
    ],
    # map.cpp keeps the out-of-line constructor these two unit-local inline copies duplicate.
    "src/editeff.cpp": [(r"^inline CMapParts::CMapParts\(\) \{ Initialize\(\); \}$", "", 1)],
    "src/editmap.cpp": [(r"^inline CMapParts::CMapParts\(\) \{ Initialize\(\); \}$", "", 1)],
    "src/inventmn.cpp": [(r"\(0x80UL << 32\)", "(0x80ULL << 32)", 1)],
    "src/memcard.cpp": [(U64, "u64", 4)],
    "src/menuchr.cpp": [(U64, "u64", 2)],
    "src/mg_drawprim.cpp": [(U64, "u64", 2)],
    "src/mg_visual.cpp": [(U64, "u64", 15)],
    "src/movie.cpp": [(r"\blong pts, long dts\b", "s64 pts, s64 dts", 1)],
    "src/nd_meswin.cpp": [(r"\(unsigned long\)", "(u64)", 6)],
    "src/password.cpp": [
        (U64, "u64", 5),
        (r"^(\s+)long (result|weight)\b", r"\1s64 \2", 2),
        (r"%lu", "%llu", 1),
    ],
    "src/userdata.cpp": [(U64, "u64", 2)],
    # An address stored in a 32-bit field: a pointer does not fit unsigned long here.
    "src/water.cpp": [(r"\(u_int\) \(unsigned long\) base", "(u_int) (u64) base", 2)],
    "include/crandom.hpp": [(r"^float abs\(float value\);$", "#include <stdlib.h>", 1)],
    "include/dbg_font.hpp": [(r"\bunsigned long(\s+prev_serno;)", r"u64\1", 1)],
    "include/menuchr.hpp": [(U64, "u64", 1)],
    "include/mg_drawprim.hpp": [(U64, "u64", 2)],
    "include/movie.hpp": [
        (r"^(\s+)long(\s+)(pts|dts|total_bytes);", r"\1s64\2\3;", 3),
        (r"\blong pts, long dts\b", "s64 pts, s64 dts", 1),
    ],
    "include/sce/libmpeg.h": [(r"^(\s+)long(\s+)(pts2nd|dts2nd|pts|dts);", r"\1s64\2\3;", 8)],
    "include/userdata.hpp": [(U64, "u64", 3)],
}


def read(path):
    # Latin-1 maps every byte to itself, so the sources round-trip whatever their encoding.
    return pathlib.Path(path).read_bytes().decode("latin-1")


def write(path, text):
    path = pathlib.Path(path)
    data = text.encode("latin-1")
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def check(key, original):
    for pattern, _, expected in RULES[key]:
        found = len(re.findall(pattern, original, re.MULTILINE))
        if found != expected:
            sys.exit(f"{key}: {pattern!r} matches {found} times, not {expected}: update scripts/port/win_rewrite.py")


def apply(key, text):
    for pattern, replacement, _ in RULES[key]:
        text = re.sub(pattern, replacement, text, flags=re.MULTILINE)
    return text


def main(argv):
    if len(argv) == 2 and argv[1] == "units":
        print(";".join(key[4:] for key in RULES if key.startswith("src/")))
        return 0
    if len(argv) == 4 and argv[1] == "unit":
        original = pathlib.Path(argv[2])
        key = "src/" + original.name
        if key in RULES:
            check(key, read(original))
            write(argv[3], apply(key, read(argv[3])))
        return 0
    if len(argv) == 4 and argv[1] == "headers":
        source, destination = pathlib.Path(argv[2]), pathlib.Path(argv[3])
        for header in sorted(path for path in source.rglob("*") if path.is_file()):
            relative = header.relative_to(source).as_posix()
            text = read(header)
            key = "include/" + relative
            if key in RULES:
                check(key, text)
                text = apply(key, text)
            write(destination / relative, text)
        return 0
    sys.exit(__doc__)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
