#!/usr/bin/env python3
"""Decode a retail PAL action script (SB2 .stb) and report its external-call sites.

    wp02_action_vm.py ext <program.stb> [ext_no ...]    list EXT sites with their pushed operands
    wp02_action_vm.py progs <program.stb>               list program table entries and functions

Layout comes from ps2/include/runscript.hpp: RS_PROG_HEADER {magic[4], main, code, prog, prog_num,
unk_14, global_num}, 12-byte vmcode_t {op, arg1, arg2}, 16-byte funcdata, 8-byte RS_PROGDATA.
Read-only; the program is never copied or dumped beyond decoded operand shapes.
"""

import pathlib
import struct
import sys

OP = {1: "LOAD", 2: "LOAD_ADDR", 3: "PUSH_CONST", 4: "POP", 5: "STORE", 6: "ADD", 7: "SUB", 8: "MUL",
      9: "DIV", 10: "MOD", 11: "NEG", 12: "ITOF", 13: "FTOI", 14: "CMP", 15: "RET", 16: "JMP",
      17: "JMP_FALSE", 18: "JMP_TRUE", 19: "CALL", 20: "PRINT", 21: "EXT", 23: "WAIT", 24: "AND",
      25: "OR", 26: "NOT", 27: "END", 28: "SKIP_END", 29: "SIN", 30: "COS"}
HEADER = struct.Struct("<4s6i")
INSN = struct.Struct("<iii")
FUNC_NAME_BYTES = 8  # host funcdata widens name; the retail file stores a 4-byte pointer slot


class Program:
    def __init__(self, data):
        if len(data) < HEADER.size:
            raise ValueError("truncated header")
        self.data = data
        (self.magic, self.main, self.code, self.prog, self.prog_num, _, self.global_num) = HEADER.unpack_from(data)
        if self.magic[:3] not in (b"SB2", b"STB"):
            raise ValueError("bad magic %r" % self.magic)
        self.version = 2 if self.magic[:3] == b"SB2" else 1  # CRunScript::load keys globals on "SB2"
        for name in ("main", "code", "prog"):
            if not 0 <= getattr(self, name) <= len(data):
                raise ValueError("%s offset outside file" % name)
        if self.prog_num < 0 or self.prog + self.prog_num * 8 > len(data):
            raise ValueError("program table outside file")

    def insn(self, off):
        """Instruction at header-relative `off`, or None when it would run past the image."""
        if off < self.code or off + INSN.size > len(self.data) or (off - self.code) % 4:
            return None
        return INSN.unpack_from(self.data, off)

    def funcdata(self, off):
        if off < 0 or off + 16 > len(self.data):
            raise ValueError("funcdata %#x outside file" % off)
        addr, name, local, arg = struct.unpack_from("<IIii", self.data, off)
        return addr, local, arg

    def function_offsets(self):
        """Header-relative funcdata offsets: main, the program table and every CALL target reached."""
        found = {self.main}
        found.update(func for _, func in self.programs())
        return found

    def instructions(self):
        """Reachable instructions as (header-relative offset, (op, arg1, arg2)), by offset.

        funcdata records sit between function bodies in the code section, so a linear 12-byte sweep loses
        alignment; walk each function from its entry along jumps and calls instead."""
        seen = {}
        pending_funcs = list(self.function_offsets())
        done_funcs = set()
        while pending_funcs:
            func = pending_funcs.pop()
            if func in done_funcs:
                continue
            done_funcs.add(func)
            addr, _, _ = self.funcdata(func)
            work = [self.code + addr]
            while work:
                off = work.pop()
                while off not in seen:
                    ins = self.insn(off)
                    if ins is None:
                        raise ValueError("instruction at %#x leaves the image" % off)
                    seen[off] = ins
                    op, a1, a2 = ins
                    if op in (15, 27):
                        break
                    if op == 16:
                        off = self.code + a1
                        continue
                    if op in (17, 18):
                        work.append(self.code + a1)
                    if op == 19:
                        pending_funcs.append(self.code + a2)
                    off += INSN.size
        return sorted(seen.items())

    def programs(self):
        for i in range(self.prog_num):
            yield struct.unpack_from("<ii", self.data, self.prog + 8 * i)


def ext_sites(program, wanted):
    """Yield (offset, ext_no, argc, operands) for each EXT whose first pushed constant is wanted.

    The compiler pushes the command number, then each argument; EXT arg1 counts all of them."""
    window = []
    for off, (op, a1, a2) in program.instructions():
        if op == 22:  # source-line marker the VM skips; it sits between a call's operand pushes
            continue
        window.append((off, op, a1, a2))
        if op != 21:
            continue
        count = a1
        pushes = [w for w in window[:-1]][-count:] if count else []
        window.clear()
        if not pushes or pushes[0][1] != 3 or pushes[0][2] != 1:
            continue
        number = pushes[0][3]
        if wanted and number not in wanted:
            continue
        operands = [(OP.get(p[1], p[1]), p[2], p[3]) for p in pushes[1:]]
        yield off, number, count - 1, operands


def ext_names(header_path):
    """ACTION_EXT_* numbers from ps2/include/actscript.hpp, so traces name the callbacks."""
    import re
    names = {}
    for match in re.finditer(r"ACTION_EXT_(\w+)\s*=\s*(-?\d+)", pathlib.Path(header_path).read_text()):
        names[int(match.group(2))] = match.group(1)
    return names


def function_for(program, number):
    for no, func in program.programs():
        if no == number:
            addr, _, local, arg = struct.unpack_from("<IIii", program.data, func)
            return func, addr, local, arg
    raise ValueError("program %d not in table" % number)


def disassemble(program, number, limit, names):
    func, addr, local, arg = function_for(program, number)
    print("prog %d funcdata %#x addr %#x local %d arg %d" % (number, func, addr, local, arg))
    pending = []
    for index in range(limit):
        off = program.code + addr + index * INSN.size
        if off + INSN.size > len(program.data):
            print("truncated at %#x" % off)
            return
        op, a1, a2 = INSN.unpack_from(program.data, off)
        note = ""
        if op in (1, 2, 3):
            pending.append(a2 if (op == 3 and a1 == 1) else None)
        if op == 21:
            note = " ; " + names.get(pending[-a1] if 0 < a1 <= len(pending) else None, "?")
            del pending[-a1:]
        print("%#06x %-10s %d %d%s" % (off - program.code, OP.get(op, op), a1, a2, note))
        if op in (15, 27):
            return


def main(argv):
    if len(argv) >= 4 and argv[1] == "dis":
        with open(argv[2], "rb") as handle:
            program = Program(handle.read())
        header = pathlib.Path(__file__).resolve().parents[2] / "ps2" / "include" / "actscript.hpp"
        disassemble(program, int(argv[3]), int(argv[4]) if len(argv) > 4 else 400, ext_names(header))
        return
    if len(argv) < 3 or argv[1] not in ("ext", "progs"):
        raise SystemExit(__doc__)
    with open(argv[2], "rb") as handle:
        program = Program(handle.read())
    if argv[1] == "progs":
        for number, func in program.programs():
            print(number, func)
        return
    wanted = {int(a) for a in argv[3:]}
    for off, number, argc, operands in ext_sites(program, wanted):
        print("%#x ext %d argc %d %s" % (off, number, argc, operands))


if __name__ == "__main__":
    try:
        main(sys.argv)
    except (OSError, ValueError) as error:
        raise SystemExit(str(error))
