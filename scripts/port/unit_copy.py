#!/usr/bin/env python3
"""Write one ps2/src unit's build copy for the port: its overridden definitions stripped, then the unit fixups
and long64 applied, in a single process.

    unit_copy.py --port-override <port_override.py> --overrides <file> [--stubs <hpp>]
                 [--define=NAME ...] [--long-is-64] SRC OUT

The output is written only when its bytes change, so an unchanged copy keeps its timestamp.
"""

import argparse
import importlib.util
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import header_fixups  # noqa: E402
import long64  # noqa: E402


def load_port_override(path: Path):
    spec = importlib.util.spec_from_file_location("port_override", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port-override", required=True, type=Path)
    parser.add_argument("--overrides", required=True, type=Path)
    parser.add_argument("--stubs", type=Path)
    parser.add_argument("--define", action="append", default=[])
    parser.add_argument("--long-is-64", action="store_true")
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    port_override = load_port_override(args.port_override)
    overrides = port_override.override_entries(port_override.read(args.overrides))
    try:
        text = port_override.strip_unit(args.source, overrides, port_override.renames(args.stubs), args.define,
                                        args.long_is_64)
    except port_override.Error as error:
        sys.exit(str(error))

    fixed = header_fixups.fixup_unit_text(args.output.name, text.encode("utf-8", errors="surrogateescape")
                                          .decode("latin-1"))
    final = long64.rewrite(fixed).encode("latin-1")
    if args.output.is_file() and args.output.read_bytes() == final:
        return
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(final)


if __name__ == "__main__":
    main()
