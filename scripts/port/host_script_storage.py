#!/usr/bin/env python3
"""Rewrite known PS2-sized script buffers in generated host source copies.

Run after port_override.py has copied/stripped each source:
    host_script_storage.py unit src/event.cpp <build>/ps2_src/event.cpp

Each exact site is count-checked so source drift stops the build instead of leaving
an under-sized runtime array behind. The helper uses host sizeof(T) and checks the
conversion to mgCMemory's signed 16-byte block count.
"""

import pathlib
import re
import sys


def replace(pattern, replacement, count, text, key):
    found = len(re.findall(pattern, text, flags=re.MULTILINE))
    if found != count:
        raise ValueError(f"{key}: expected {count} storage site(s), found {found}")
    return re.sub(pattern, replacement, text, count=count, flags=re.MULTILINE)


RULES = {
    "src/effscript.cpp": [
        (r"^(\s*)RS_CALLDATA  \*call_data = \(RS_CALLDATA \*\) memory->Alloc\(1\);$",
         r'\1RS_CALLDATA  *call_data = port_script_storage::allocate<RS_CALLDATA>(memory, 2, "SetEffectScript");', 1),
    ],
}


def transform(key, text):
    if key not in RULES:
        raise ValueError(f"no host script storage rules for {key}")
    for pattern, replacement, count in RULES[key]:
        text = replace(pattern, replacement, count, text, key)

    include = '#include "script_storage.hpp"\n'
    if include not in text:
        text = include + text
    return text


def main(argv):
    if len(argv) != 4 or argv[1] != "unit":
        raise SystemExit(__doc__)
    key, path = argv[2], pathlib.Path(argv[3])
    source = path.read_bytes().decode("latin-1")
    updated = transform(key, source).encode("latin-1")
    if path.read_bytes() != updated:
        path.write_bytes(updated)


if __name__ == "__main__":
    try:
        main(sys.argv)
    except (OSError, ValueError) as error:
        raise SystemExit(str(error))
