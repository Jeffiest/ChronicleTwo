#!/usr/bin/env python3
"""Check that the local override scanner matches 64-bit long signatures safely."""

from pathlib import Path
import subprocess
import sys
import tempfile


SCRIPT = Path(__file__).with_name("port_override.py")
MARKER = "host override removed this body"


def run(*args):
    return subprocess.run([sys.executable, str(SCRIPT), *map(str, args)], capture_output=True, text=True)


def main():
    with tempfile.TemporaryDirectory(prefix="port-override-long64-") as directory:
        root = Path(directory)
        overrides = root / "overrides.txt"
        with overrides.open("w", encoding="utf-8", newline="") as stream:
            stream.write("A::F(unsigned long long)\n")
        ps2 = root / "ps2"
        ps2.mkdir()
        source = ps2 / "a.cpp"
        source.write_text(f"void A::F(unsigned long value) {{ {MARKER}; }}\n", encoding="utf-8")
        stubs = root / "stubs"
        stubs.mkdir()

        checked = run("check", "--overrides", overrides, "--stubs", stubs,
                      "--long-is-64", ps2, "-o", root / "checked")
        if checked.returncode:
            raise SystemExit(checked.stderr)
        stripped = run("strip", "--overrides", overrides, "--long-is-64", source, root / "stripped.cpp")
        if stripped.returncode:
            raise SystemExit(stripped.stderr)
        if MARKER in (root / "stripped.cpp").read_text(encoding="utf-8"):
            raise SystemExit("long-is-64 did not remove the matching PS2 definition")

        mismatch = run("check", "--overrides", overrides, "--stubs", stubs, ps2, "-o", root / "mismatch")
        if mismatch.returncode == 0 or "no ps2/src unit defines it under that signature" not in mismatch.stderr:
            raise SystemExit("long-is-64 mismatch was not rejected")

    print("port_override long64 regression passed")


if __name__ == "__main__":
    main()
