#!/usr/bin/env python3
"""Reject a reviewed byte-stride pointer-array pattern in generated/native host code."""

import argparse
from pathlib import Path

from compatibility_audit import POINTER_ARRAY_OFFSET, SOURCE_SUFFIXES


def findings(generated, host):
    generated_files = sorted(generated.rglob("*.cpp")) if generated.is_dir() else []
    if not generated_files:
        raise ValueError(f"no generated host translation units in {generated}")
    if not host.is_dir():
        raise ValueError(f"missing native host source tree: {host}")
    native_files = sorted(path for path in host.rglob("*")
                          if path.suffix in SOURCE_SUFFIXES and "tests" not in path.relative_to(host).parts)
    result = []
    for path in generated_files + native_files:
        for number, line in enumerate(path.read_text(encoding="latin-1").splitlines(), 1):
            if POINTER_ARRAY_OFFSET.search(line):
                result.append(f"{path}:{number}: {line.strip()}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated", type=Path, required=True)
    parser.add_argument("--host", type=Path, required=True)
    args = parser.parse_args()
    try:
        unsafe = findings(args.generated, args.host)
    except ValueError as error:
        parser.exit(1, f"host pointer-array check: {error}\n")
    if unsafe:
        parser.exit(1, "host pointer-array check: byte-stride pointer access remains:\n" +
                    "\n".join(unsafe) + "\n")
    print("host pointer-array check: reviewed lexical pattern absent")


if __name__ == "__main__":
    main()
