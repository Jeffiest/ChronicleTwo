#!/usr/bin/env python3
"""Focused regression for host script buffer sizing and generated-source coverage."""

import ctypes
import pathlib
import re
import sys

from host_script_storage import transform


ROOT = pathlib.Path(__file__).resolve().parents[2]
PS2 = ROOT / "ps2" / "src"


class StackValue(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int), ("value", ctypes.c_void_p)]


class CallFrame(ctypes.Structure):
    _fields_ = [("ret", ctypes.c_void_p), ("frame", ctypes.c_void_p), ("func", ctypes.c_void_p)]


def blocks(element, count):
    return (ctypes.sizeof(element) * count + 15) // 16


def check_capacity_and_ranges():
    assert ctypes.sizeof(StackValue) == 16
    assert ctypes.sizeof(CallFrame) == 24
    builders = {
        "event": (128, 512, 64, 384),
        "action": (128, 512, 64, 384),
        "effect": (64, 2, 32, 1),
        "monster": (128, 512, 64, 384),
    }
    for name, (stack_count, call_count, old_stack_blocks, old_call_blocks) in builders.items():
        stack_blocks = blocks(StackValue, stack_count)
        call_blocks = blocks(CallFrame, call_count)
        assert stack_blocks >= old_stack_blocks
        assert call_blocks >= old_call_blocks
        stack_start = 0x100000
        call_start = stack_start + stack_blocks * 16
        assert stack_start + stack_count * ctypes.sizeof(StackValue) <= call_start
        assert call_start + call_count * ctypes.sizeof(CallFrame) <= call_start + call_blocks * 16


def check_generated_rewrites_and_load_builders():
    filename = "effscript.cpp"
    original = (PS2 / filename).read_text(encoding="latin-1")
    updated = transform("src/effscript.cpp", original)
    assert '#include "script_storage.hpp"' in updated
    assert 'port_script_storage::allocate<RS_CALLDATA>(memory, 2, "SetEffectScript")' in updated
    assert "port_script_storage::allocate<RS_STACKDATA>" not in updated
    tables = [line for line in original.splitlines() if "ext_func[" in line]
    assert tables and all(line in updated for line in tables)

    expected_sites = {
        "event.cpp": ["sizeof(RS_STACKDATA) * 0x80", "sizeof(RS_CALLDATA) * 0x200"],
        "actscript.cpp": ["sizeof(RS_STACKDATA) * 0x80", "sizeof(RS_CALLDATA) * 0x200"],
        "effscript.cpp": ["sizeof(RS_STACKDATA) * 0x40"],
        "runscript_opcodes.cpp": ["sizeof(RS_STACKDATA) * 128", "sizeof(RS_CALLDATA) * 512"],
    }
    for source_name, expressions in expected_sites.items():
        source = (PS2 / source_name).read_text(encoding="latin-1")
        assert all(expression in source for expression in expressions), (source_name, expressions)

    load_sites = []
    for path in PS2.rglob("*.cpp"):
        source = path.read_text(encoding="latin-1")
        load_sites.extend((path.name, line.strip()) for line in source.splitlines()
                          if re.search(r"(?:->|\.)load\s*\(", line))
    assert len(load_sites) == 5, load_sites
    assert {name for name, _ in load_sites} == {
        "event.cpp", "actscript.cpp", "effscript.cpp", "event_func.cpp", "runscript_opcodes.cpp"
    }, load_sites
    event_args = (PS2 / "event_func.cpp").read_text(encoding="latin-1")
    assert "RS_STACKDATA stack[EVENT_ARG_STACK_NUM]" in event_args
    assert "RS_CALLDATA  calls[EVENT_ARG_CALL_NUM]" in event_args
    assert "script.load((RS_PROG_HEADER *) program, stack, EVENT_ARG_STACK_NUM, calls, EVENT_ARG_CALL_NUM)" in event_args


if __name__ == "__main__":
    try:
        check_capacity_and_ranges()
        check_generated_rewrites_and_load_builders()
    except AssertionError as error:
        print(f"script storage regression failed: {error}", file=sys.stderr)
        raise
    print("script storage regression passed")
