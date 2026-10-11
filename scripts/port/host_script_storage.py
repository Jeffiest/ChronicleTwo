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
    "src/event.cpp": [
        (r"^(\s*)stack_data = \(RS_STACKDATA \*\) memory->Alloc\(0x40\);$",
         r'\1stack_data = port_script_storage::allocate<RS_STACKDATA>(memory, 0x80, "SetEventScript");', 1),
        (r"^(\s*)call_data = \(RS_CALLDATA \*\) memory->Alloc\(0x180\);$",
         r'\1call_data = port_script_storage::allocate<RS_CALLDATA>(memory, 0x200, "SetEventScript");', 1),
    ],
    "src/actscript.cpp": [
        (r"^(\s*)RS_STACKDATA \*stack = reinterpret_cast<RS_STACKDATA \*>\(memory->Alloc\(0x40\)\);$",
         r'\1RS_STACKDATA *stack = port_script_storage::allocate<RS_STACKDATA>(memory, 0x80, "SetActionScript");', 1),
        (r"^(\s*)RS_CALLDATA  \*call_data = reinterpret_cast<RS_CALLDATA \*>\(memory->Alloc\(0x180\)\);$",
         r'\1RS_CALLDATA  *call_data = port_script_storage::allocate<RS_CALLDATA>(memory, 0x200, "SetActionScript");', 1),
    ],
    "src/effscript.cpp": [
        (r"^(\s*)RS_STACKDATA \*stack = \(RS_STACKDATA \*\) memory->Alloc\(0x20\);$",
         r'\1RS_STACKDATA *stack = port_script_storage::allocate<RS_STACKDATA>(memory, 0x40, "SetEffectScript");', 1),
        (r"^(\s*)RS_CALLDATA  \*call_data = \(RS_CALLDATA \*\) memory->Alloc\(1\);$",
         r'\1RS_CALLDATA  *call_data = port_script_storage::allocate<RS_CALLDATA>(memory, 2, "SetEffectScript");', 1),
    ],
    "src/runscript_opcodes.cpp": [
        (r"^(\s*)RS_STACKDATA \*value_stack = \(RS_STACKDATA \*\) memory->Alloc\(0x40\);$",
         r'\1RS_STACKDATA *value_stack = port_script_storage::allocate<RS_STACKDATA>(memory, 128, "SetMonsterScript");', 1),
        (r"^(\s*)RS_CALLDATA  \*call_stack = \(RS_CALLDATA \*\) memory->Alloc\(0x180\);$",
         r'\1RS_CALLDATA  *call_stack = port_script_storage::allocate<RS_CALLDATA>(memory, 512, "SetMonsterScript");', 1),
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
