"""Run native compatibility fixtures, including required-allocation failure paths."""

import argparse
import subprocess


def run(executable, case=None, diagnostic=None, timeout=15):
    command = [executable] + ([case] if case else [])
    result = subprocess.run(command, text=True, capture_output=True, timeout=timeout)
    output = result.stdout + result.stderr
    label = case or executable
    if diagnostic:
        if result.returncode == 0 or diagnostic not in output:
            raise RuntimeError(f"{label}: expected termination containing {diagnostic!r}; "
                               f"exit={result.returncode}\n{output}")
    elif result.returncode != 0:
        raise RuntimeError(f"{label}: exit={result.returncode}\n{output}")
    print(f"PASS {label}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("arena", "scripts", "monsters", "input", "menus", "options", "villagers", "photos"):
        parser.add_argument(f"--{name}", required=True)
    parser.add_argument("--capture")
    parser.add_argument("--skip-capture", action="store_true", help="run without a Vulkan device")
    args = parser.parse_args()
    if not args.skip_capture and not args.capture:
        parser.error("--capture is required unless --skip-capture is specified")
    run(args.arena, "zero-capacity", "allocation overflow")
    for case in ("bounds-growth", "invalid-reset-align", "optional-null", "reset-growth", "diagnostics-lifecycle"):
        run(args.arena, case)
    run(args.arena, "growth-failure", "allocation overflow")
    run(args.arena, "suppressed-growth-failure", "allocation overflow")
    run(args.scripts)
    run(args.scripts, "null-arena", "script storage has no arena")
    run(args.scripts, "small-arena", "script storage allocation failed")
    run(args.scripts, "size-overflow", "script storage size overflow")
    run(args.monsters)
    run(args.input)
    if not args.skip_capture:
        run(args.capture, timeout=45)
    for case in ("typed-names-and-content", "background-read", "invalid-missing", "insufficient-capacity"):
        run(args.menus, case)
    run(args.options)
    run(args.villagers)
    run(args.photos)
    run(args.photos, "negative-count", "photo-name storage: PIC_INFO count")
    run(args.photos, "oversized-count", "photo-name storage: PIC_INFO count")
    run(args.photos, "arithmetic-overflow", "photo-name storage: record byte-size overflow")
    run(args.photos, "required-allocation-failure", "photo-name storage: required PIC_INFO record allocation failed")
    run(args.photos, "required-name-failure", "photo-name storage: required PIC_NAME string allocation failed")


if __name__ == "__main__":
    main()
