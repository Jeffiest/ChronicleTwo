#!/usr/bin/env python3
"""Apply checked host-width pointer fixes to generated menu source copies."""

import pathlib
import re
import sys


def replace(pattern, replacement, count, text, key):
    found = len(re.findall(pattern, text, flags=re.MULTILINE))
    if found != count:
        raise ValueError(f"{key}: expected {count} pointer site(s), found {found}")
    return re.sub(pattern, replacement, text, count=count, flags=re.MULTILINE)


def transform(key, text):
    if key != "src/menuop.cpp":
        raise ValueError(f"no host menu pointer rules for {key}")

    text = replace(r"^    int {18}offset;$", "", 1, text, key)
    text = replace(
        r"^([ \t]*)for \(i = 0, offset = 0; i < OPTION_BUTTON_NUM; i\+\+, offset \+= 4\) \{$",
        r"\1for (i = 0; i < OPTION_BUTTON_NUM; i++) {", 1, text, key)
    text = replace(
        r"^([ \t]*)EnableButton\(\*\(MENUFORMPARTS_TYPE \*\*\) \(\(u8 \*\) loop_row \+ offset\)\);$",
        r"\1EnableButton(loop_row[i]);", 1, text, key)

    return text


def extract_function(text, name):
    signature = f"void CMenuOption::{name}("
    if text.count(signature) != 1:
        raise ValueError(f"src/menuop.cpp: expected one definition of {signature}")
    start = text.index(signature)
    opening = text.find("{", start)
    if opening < 0:
        raise ValueError(f"src/menuop.cpp: missing body for {signature}")

    depth = 0
    in_string = False
    in_char = False
    escaped = False
    line_comment = False
    block_comment = False
    index = opening
    while index < len(text):
        char = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if line_comment:
            if char == "\n":
                line_comment = False
        elif block_comment:
            if char == "*" and following == "/":
                block_comment = False
                index += 1
        elif in_string or in_char:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif in_string and char == '"':
                in_string = False
            elif in_char and char == "'":
                in_char = False
        elif char == "/" and following == "/":
            line_comment = True
            index += 1
        elif char == "/" and following == "*":
            block_comment = True
            index += 1
        elif char == '"':
            in_string = True
        elif char == "'":
            in_char = True
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
        index += 1
    raise ValueError(f"src/menuop.cpp: unclosed body for {signature}")


def fixture_source(transformed_text):
    if "EnableButton(loop_row[i]);" not in transformed_text:
        raise ValueError("src/menuop.cpp: fixture source requires the transformed pointer expression")
    methods = [extract_function(transformed_text, name)
               for name in ("DefaultButton", "EnableButton", "UpdateOptionForm")]
    prefix = '''#include "mainloop.hpp"\n#include "menuop.hpp"\n\nstatic CMenuPosDataForm *OptionButtonForm = NULL;\n\n'''
    suffix = '''\n\nvoid PortSetMenuOptionFormForTest(int enabled) {\n    OptionButtonForm = enabled ? reinterpret_cast<CMenuPosDataForm *>(1) : NULL;\n}\n'''
    return prefix + "\n\n".join(methods) + suffix


def main(argv):
    if len(argv) == 4 and argv[1] == "unit":
        key, path = argv[2], pathlib.Path(argv[3])
        original = path.read_bytes()
        updated = transform(key, original.decode("latin-1")).encode("latin-1")
        if original != updated:
            path.write_bytes(updated)
        return
    if len(argv) == 5 and argv[1] == "fixture":
        key, source, destination = argv[2], pathlib.Path(argv[3]), pathlib.Path(argv[4])
        if key != "src/menuop.cpp":
            raise ValueError(f"no host menu pointer fixture rules for {key}")
        updated = fixture_source(source.read_bytes().decode("latin-1")).encode("latin-1")
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.exists() or destination.read_bytes() != updated:
            destination.write_bytes(updated)
        return
    raise SystemExit(__doc__)


if __name__ == "__main__":
    try:
        main(sys.argv)
    except (OSError, ValueError) as error:
        raise SystemExit(str(error))
