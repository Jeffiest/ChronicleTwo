#!/usr/bin/env python3
"""Inject the bounded host photo-name arena setup into generated inventmn.cpp."""

import pathlib
import re
import sys


INCLUDE = '#include "photo_name_storage.hpp"'
ASSIGNMENT = "pic_name_info_num = spiGetStackInt(stack);"
RECORD_ALLOC = "PicNameStack->Alloc(blocks + 2)"
NAME_ALLOC = "entry->name = mgCopyString(text, PicNameStack);"
INJECTED = (
    "pic_name_info_num = static_cast<short>(port_photo_name_storage::checked_record_count("
    "spiGetStackInt(stack)));\n"
    "    port_photo_name_storage::prepare(PicNameStack, pic_name_info_num, sizeof(PIC_NAME_INFO));"
)


def transform(source: str) -> str:
    if source.count("static mgCMemory *PicNameStack;") != 1:
        raise ValueError("expected the original private PicNameStack global")
    if source.count('stack.stSetBuffer(pic_name_text_buff_1660, 0x248);') != 1:
        raise ValueError("expected the original 584-block retail arena binding")
    if source.count('{"PIC_INFO", _PIC_INFO}') != 1 or source.count('{"PIC_NAME", _PIC_NAME}') != 1:
        raise ValueError("expected the original PIC_INFO/PIC_NAME opcode entries")
    has_include = INCLUDE in source
    has_setup = "port_photo_name_storage::prepare(" in source
    if has_include or has_setup:
        if (has_include and has_setup and source.count(INCLUDE) == 1 and source.count("port_photo_name_storage::prepare(") == 1
                and ASSIGNMENT not in source
                and "port_photo_name_storage::checked_record_count(spiGetStackInt(stack))" in source
                and "port_photo_name_storage::allocate_records(PicNameStack, blocks, pic_name_info_num)" in source
                and "port_photo_name_storage::copy_required_name(text, PicNameStack, pic_name_info_num)" in source):
            return source
        raise ValueError("partial photo-name host rewrite already present")
    if source.count(ASSIGNMENT) != 1:
        raise ValueError("expected exactly one PIC_INFO count assignment")
    if source.count(RECORD_ALLOC) != 1:
        raise ValueError("expected exactly one required PIC_INFO record allocation")
    if source.count(NAME_ALLOC) != 1:
        raise ValueError("expected exactly one required PIC_NAME string allocation")
    transformed = source.replace(ASSIGNMENT, INJECTED, 1).replace(
        RECORD_ALLOC, "port_photo_name_storage::allocate_records(PicNameStack, blocks, pic_name_info_num)", 1).replace(
        NAME_ALLOC, "entry->name = port_photo_name_storage::copy_required_name(text, PicNameStack, pic_name_info_num);", 1)
    include_anchor = '#include "inventmn.hpp"'
    if transformed.count(include_anchor) != 1:
        raise ValueError("expected exactly one inventmn.hpp include anchor")
    return transformed.replace(include_anchor, include_anchor + "\n" + INCLUDE, 1)


def main(argv):
    if len(argv) != 2:
        raise SystemExit("usage: host_photo_names.py GENERATED_INVENTMN_CPP")
    path = pathlib.Path(argv[1])
    original = path.read_text(encoding="latin-1")
    updated = transform(original)
    if updated != original:
        path.write_text(updated, encoding="latin-1", newline="")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv))
    except ValueError as error:
        raise SystemExit(str(error))
