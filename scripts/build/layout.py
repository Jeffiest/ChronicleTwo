#!/usr/bin/env python3
"""What ps2/config/<region>/main.yaml says about the image: its translation
units in link order, the sections each one owns, and the symbols in them.

    layout.py --list-units     print `kind<TAB>unit<TAB>source<TAB>reference`
    layout.py --link-order     print the objects in link order
    layout.py --list-assembled print `kind<TAB>reference<TAB>object<TAB>objcopy args`
                               for every object assembled from splat's output

A unit is `asm` when its yaml entry is `asmtu` (linked from the one file splat
writes for it) and `cpp` when it is compiled from ps2/src/<unit>.cpp.
"""

import argparse
import bisect
import os
import re
import struct
import sys
from pathlib import Path

REGION = os.environ.get("REGION", "PAL").lower()
ELF_NAMES = {"pal": "SCES_511.90"}
BASENAME = ELF_NAMES[REGION]
ELF_PATH = Path(f"rom/{REGION}/extracted/iso/{BASENAME}")
CONFIG = Path(f"ps2/config/{REGION}")
YAML = CONFIG / "main.yaml"
SYMBOLS = CONFIG / "main.symbols.txt"
ASM = Path(f"ps2/asm/{REGION}")
SRC = Path("ps2/src")
BUILD = Path(f"build/{REGION}")

VRAM = 0x00100000
FILE_OFFSET = 0x80
# Where the file image stops; .sbss and .bss follow and take no bytes.
FILE_END = 0x0037CD80

# Retail's sections, (name, start, end). `.init`, `.ctor` and `.vtables` are
# the three runs retail's linker script places after `.rodata`.
SECTIONS = (
    (".text", 0x00100000, 0x00325C80),
    (".vutext", 0x00325C80, 0x0032A320),
    (".data", 0x0032A320, 0x00363480),
    (".vudata", 0x00363480, 0x00363580),
    (".rodata", 0x00363580, 0x00379680),
    (".init", 0x00379680, 0x0037AFE0),
    (".ctor", 0x0037AFE0, 0x0037B0B0),
    (".vtables", 0x0037B0B0, 0x0037C6F0),
    (".rdata", 0x0037C6F0, 0x0037C700),
    (".sdata", 0x0037C700, 0x0037CD80),
    (".sbss", 0x0037CD80, 0x0037EAD5),
    (".bss", 0x0037EAD5, 0x01F64A00),
)
NOBITS = (".sbss", ".bss")
# Sections reached through $gp.
SMALL = (".sdata", ".sbss")

# Symbols the linker script defines; they label an address and own no bytes.
LINKER_SYMBOLS = {
    "__data_start", "__static_init", "__static_init_end",
    "__exception_table_start__", "__exception_table_end__",
    "_overlay_group_addresses", "__data_end", "_fbss", "__bss_start", "_gp",
    "__bss_end", "_end", "end", "_stack", "_stack_size", "_heap_size",
    "_align_segment", "__data_size", "__bss_size",
}

SUBSEGMENT = re.compile(
    r"^\s*-\s*\{start:\s*(0x[0-9a-fA-F]+),\s*type:\s*([^,}\s]+),\s*name:\s*([^,}\s]+)(.*)\}\s*$")
VRAM_FIELD = re.compile(r"vram:\s*(0x[0-9a-fA-F]+)")
LINKER_SECTION = re.compile(r"linker_section:\s*([^,}\s]+)")
SYMBOL_ROW = re.compile(r"^(\S+)\s*=\s*(0x[0-9A-Fa-f]+)\s*;(?:\s*//\s*(.*))?$")

UNIT_KINDS = {"asmtu": "asm", "asm": "asm", "cpp": "cpp", "c": "cpp"}
TYPE_SECTION = {"asmtu": ".text", "asm": ".text", "cpp": ".text", "c": ".text",
                ".data": ".data", "data": ".data", ".rodata": ".rodata",
                ".sinit": ".init", ".sdata": ".sdata", ".sbss": ".sbss",
                ".bss": ".bss", "rdata": ".rdata"}


def section_of(address):
    for name, lo, hi in SECTIONS:
        if lo <= address < hi:
            return name
    return None


def section_range(name):
    return next((lo, hi) for n, lo, hi in SECTIONS if n == name)


class Layout:
    """main.yaml's subsegments, as address ranges per unit and section."""

    def __init__(self, yaml=YAML):
        rows = []
        for line in Path(yaml).read_text().splitlines():
            m = SUBSEGMENT.match(line)
            if not m:
                continue
            start, kind, name, rest = m.groups()
            vram = VRAM_FIELD.search(rest)
            address = int(vram.group(1), 16) if vram else int(start, 16) - FILE_OFFSET + VRAM
            section = LINKER_SECTION.search(rest)
            section = section.group(1) if section else TYPE_SECTION[kind]
            rows.append((address, kind, name, section))
        self.rows = rows
        # (unit, section) -> (start, end); a run ends where the next row
        # starts, or where its section does.
        self.ranges = {}
        self.kinds = {}
        self.order = []
        for i, (address, kind, name, section) in enumerate(rows):
            end = rows[i + 1][0] if i + 1 < len(rows) else SECTIONS[-1][2]
            end = min(end, section_range(section)[1])
            self.ranges[(name, section)] = (address, end)
            if kind in UNIT_KINDS:
                self.kinds[name] = UNIT_KINDS[kind]
                self.order.append(name)
        # A unit with data and no code: a library object, or one of the
        # section-wide blobs. It is linked from the dump splat writes for it.
        self.data_only = []
        for address, kind, name, section in rows:
            if name not in self.kinds and name not in self.data_only:
                self.data_only.append(name)

    def units(self, kind=None):
        return [u for u in self.order if kind is None or self.kinds[u] == kind]

    def sections(self, unit):
        """[(section, start, end)] the unit owns, in address order."""
        out = [(s, lo, hi) for (u, s), (lo, hi) in self.ranges.items() if u == unit]
        return sorted(out, key=lambda r: r[1])

    def section_units(self, section):
        """[(unit, start, end)] holding part of the section, in address order."""
        out = [(u, lo, hi) for (u, s), (lo, hi) in self.ranges.items() if s == section]
        return sorted(out, key=lambda r: r[1])

    def object_path(self, unit):
        if self.kinds.get(unit) == "cpp":
            return f"{BUILD}/obj/{unit}.cpp.o"
        return f"{BUILD}/obj/{self.object_stem(unit)}.s.o"

    def object_stem(self, unit):
        """The unit's path with a file name no other unit's object shares.

        A linker script names an object by its file name alone, so two units
        called the same in different directories (libkernl's exit and libc's)
        each take their directory's name as a prefix.
        """
        head, name = os.path.split(unit)
        if sum(os.path.basename(u) == name for u in self.order) < 2:
            return unit
        return os.path.join(head, f"{os.path.basename(head)}_{name}")

    def data_object_path(self, unit, section):
        """The object a data-only unit's one section is assembled into."""
        kind = next(k for _a, k, n, s in self.rows if n == unit and s == section)
        return f"{BUILD}/obj/data/{unit}.{kind.lstrip('.')}.s.o"

    def reference(self, unit):
        return f"{ASM}/{unit}.s"

    def source(self, unit):
        return f"{SRC}/{unit}.cpp"

    def dump_path(self, unit, section):
        """Where splat writes a data-only unit's one section."""
        kind = next(k for _a, k, n, s in self.rows if n == unit and s == section)
        return f"{ASM}/data/{unit}.{kind.lstrip('.')}.s"


def read_symbols(path=SYMBOLS):
    """[(address, name, size, is_function)] sorted by address."""
    rows = []
    for line in Path(path).read_text().splitlines():
        m = SYMBOL_ROW.match(line)
        if not m:
            continue
        name, address, attrs = m.group(1), int(m.group(2), 16), m.group(3) or ""
        size = re.search(r"size:(0x[0-9a-fA-F]+)", attrs)
        rows.append((address, name, int(size.group(1), 16) if size else 0, "type:func" in attrs))
    rows.sort()
    return rows


class Retail:
    """The retail executable's image and relocations."""

    def __init__(self, path=ELF_PATH):
        data = Path(path).read_bytes()
        shoff, = struct.unpack_from("<I", data, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
        sections = []
        for i in range(shnum):
            fields = struct.unpack_from("<10I", data, shoff + i * shentsize)
            sections.append(dict(zip(("name", "type", "flags", "addr", "offset", "size",
                                      "link", "info", "align", "entsize"), fields)))
        names = sections[shstrndx]["offset"]
        for s in sections:
            s["name"] = data[names + s["name"]:data.index(b"\0", names + s["name"])].decode()
        main = next(s for s in sections if s["name"] == "main")
        self.base = main["addr"]
        self.image = data[main["offset"]:main["offset"] + main["size"]]
        rel = next(s for s in sections if s["name"] == ".relmain")
        self.relocations = {}
        for k in range(rel["size"] // 8):
            offset, info = struct.unpack_from("<II", data, rel["offset"] + k * 8)
            self.relocations[offset] = info & 0xFF

    def bytes(self, lo, hi):
        return self.image[lo - self.base:hi - self.base]

    def word(self, address):
        return struct.unpack_from("<I", self.image, address - self.base)[0]


class SymbolIndex:
    def __init__(self, rows=None):
        self.rows = read_symbols() if rows is None else rows
        self.addresses = [r[0] for r in self.rows]
        self.by_name = {r[1]: r for r in self.rows}

    def within(self, lo, hi):
        i = bisect.bisect_left(self.addresses, lo)
        j = bisect.bisect_left(self.addresses, hi)
        return self.rows[i:j]

    def at_or_before(self, address):
        i = bisect.bisect_right(self.addresses, address) - 1
        return self.rows[i] if i >= 0 else None


DIR_FIELD = re.compile(r"dir:\s*([^,}\s]+)")


def address_alignment(address, cap=16):
    """The largest power of two, up to cap, that an address is a multiple of."""
    align = 1
    while align < cap and address % (align * 2) == 0:
        align *= 2
    return align


def assembled_objects(layout, yaml=YAML):
    """[(kind, reference, object, objcopy args)] for every object assembled
    from splat's output: each `asm` unit's whole-unit file, and each section of
    a data-only unit from the dump splat writes for it (under the subsegment's
    `dir:`, when it has one). Objects are relative to the build directory.

    Each section is given the alignment its retail address has, capped at the
    sixteen bytes GNU as gives a section by default, so the linker never pads
    in front of it.
    """
    dirs = {}
    for line in Path(yaml).read_text().splitlines():
        m = SUBSEGMENT.match(line)
        d = DIR_FIELD.search(m.group(4)) if m else None
        if d:
            dirs[(m.group(3), m.group(2))] = d.group(1)

    def args(unit, sections=None):
        return [f"--set-section-alignment {s}={address_alignment(lo)}"
                for s, lo, _hi in layout.sections(unit) if sections is None or s in sections]

    def relative(path):
        return os.path.relpath(path, BUILD)

    out = []
    for unit in layout.units("asm"):
        out.append(("asm", layout.reference(unit), relative(layout.object_path(unit)), args(unit)))
    for unit in layout.data_only:
        for _address, kind, name, section in layout.rows:
            if name != unit:
                continue
            subdir = dirs.get((name, kind))
            dump = f"{ASM}/data/{subdir + '/' if subdir else ''}{unit}.{kind.lstrip('.')}.s"
            obj = relative(layout.data_object_path(unit, section))
            out.append(("data", dump, obj, args(unit, (section,))))
    return out


def main():
    os.chdir(os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir, os.pardir)))
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--list-units", action="store_true")
    ap.add_argument("--link-order", action="store_true")
    ap.add_argument("--list-assembled", action="store_true")
    args = ap.parse_args()
    layout = Layout()
    if args.list_units:
        for unit in layout.units():
            kind = layout.kinds[unit]
            source = layout.source(unit) if kind == "cpp" else layout.reference(unit)
            print(f"{kind}\t{unit}\t{source}\t{layout.reference(unit)}")
        return 0
    if args.link_order:
        for unit in layout.units():
            print(layout.object_path(unit))
        return 0
    if args.list_assembled:
        for kind, reference, obj, objcopy in assembled_objects(layout):
            print(f"{kind}\t{reference}\t{obj}\t{' '.join(objcopy)}")
        return 0
    ap.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
