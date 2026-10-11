#!/usr/bin/env python3
"""Regression checks for the generated host photo-name storage rewrite."""

import pathlib
import sys

from host_photo_names import INCLUDE, transform

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "ps2" / "src" / "inventmn.cpp"


def run():
    original = SOURCE.read_text(encoding="latin-1")
    rewritten = transform(original)
    assert INCLUDE in rewritten
    assert "port_photo_name_storage::checked_record_count(spiGetStackInt(stack))" in rewritten
    assert "port_photo_name_storage::prepare(PicNameStack, pic_name_info_num, sizeof(PIC_NAME_INFO));" in rewritten
    assert "port_photo_name_storage::allocate_records(PicNameStack, blocks, pic_name_info_num)" in rewritten
    assert "port_photo_name_storage::copy_required_name(text, PicNameStack, pic_name_info_num)" in rewritten
    assert "static mgCMemory *PicNameStack;" in rewritten
    assert '{"PIC_INFO", _PIC_INFO}' in rewritten
    assert '{"PIC_NAME", _PIC_NAME}' in rewritten
    assert "stack.stSetBuffer(pic_name_text_buff_1660, 0x248);" in rewritten
    assert transform(rewritten) == rewritten
    for invalid in (rewritten.replace("static mgCMemory *PicNameStack;", "", 1),
                    rewritten.replace("{\"PIC_INFO\", _PIC_INFO}", "", 1)):
        try:
            transform(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError("rewrite accepted changed private globals/opcode table")


if __name__ == "__main__":
    try:
        run()
    except AssertionError as error:
        print(f"host photo names regression failed: {error}", file=sys.stderr)
        raise
    print("host photo names regression passed")
