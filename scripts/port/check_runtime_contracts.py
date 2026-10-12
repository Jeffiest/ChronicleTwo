#!/usr/bin/env python3
"""Compiler-AST gate for active MDT typed allocation widths."""

from __future__ import annotations

import argparse
import json
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path


def ast_for(source: Path, database: Path, clang: Path | None = None,
            compile_source: Path | None = None) -> str:
    entries = json.loads(database.read_text(encoding="utf-8"))
    compile_source = (compile_source or source).resolve()
    entry = next((e for e in entries if Path(e["file"]).resolve() == compile_source), None)
    if entry is None:
        raise ValueError(f"source is not present in compile database: {source}")
    argv = shlex.split(entry["command"], posix=False)
    compiler = str(clang or Path(argv[0]))
    args = argv[1:]
    cleaned = []
    skip = False
    for arg in args:
        if skip:
            skip = False
            continue
        if arg in ("-o", "-c"):
            skip = arg == "-o"
            continue
        if arg == str(compile_source) or Path(arg).resolve() == compile_source:
            continue
        cleaned.append(arg)
    command = [compiler, *cleaned, "-Xclang", "-ast-dump=json", "-Xclang",
               "-ast-dump-filter=CopyMDTDataPointer", "-fsyntax-only", str(source)]
    result = subprocess.run(command, cwd=entry["directory"], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise ValueError(f"Clang AST failed ({result.returncode}): {result.stderr[-3000:]}")
    return result.stdout


def check_ast(ast: str) -> tuple[bool, str]:
    def walk(node):
        yield node
        for child in node.get("inner", []):
            yield from walk(child)

    def unwrap(node):
        while node.get("kind") in {"ImplicitCastExpr", "CXXStaticCastExpr", "ParenExpr"}:
            children = node.get("inner", [])
            if len(children) != 1:
                break
            node = children[0]
        return node

    # Clang emits separate JSON documents for the header declaration and definition.
    documents = []
    decoder = json.JSONDecoder()
    remaining = ast.lstrip()
    while remaining:
        document, used = decoder.raw_decode(remaining)
        documents.append(document)
        remaining = remaining[used:].lstrip()
    tree = {"inner": documents}
    checked = 0
    for node in walk(tree):
        children = node.get("inner", [])
        if node.get("kind") != "CallExpr" or len(children) != 3:
            continue
        if not any(n.get("referencedDecl", {}).get("name") == "RequiredModelStorage"
                   for n in walk(children[0])):
            continue
        size = unwrap(children[2])
        if not any(n.get("kind") == "MemberExpr" and n.get("name") == "material_num"
                   for n in walk(size)):
            continue
        operands = size.get("inner", [])
        if size.get("kind") != "BinaryOperator" or size.get("opcode") != "*" or len(operands) != 2:
            return False, "material allocation is not count multiplied by host width"
        count, width = map(unwrap, operands)
        if count.get("kind") != "MemberExpr" or count.get("name") != "material_num":
            return False, "material allocation count differs from material_num"
        if (width.get("kind") != "UnaryExprOrTypeTraitExpr" or width.get("name") != "sizeof"
                or width.get("argType", {}).get("qualType") != "mgMaterial"):
            return False, "material allocation does not use sizeof(mgMaterial)"
        checked += 1
    return (checked == 1, f"{checked} active material allocations use count * sizeof(mgMaterial)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compile-commands", type=Path, required=True)
    parser.add_argument("--clang", type=Path)
    parser.add_argument("--negative-control", action="store_true",
                        help="mutate the active sizeof expression to sizeof(int), then require rejection")
    args = parser.parse_args()
    try:
        source = args.source.resolve()
        ast = ast_for(source, args.compile_commands.resolve(), args.clang)
        ok, message = check_ast(ast)
        if args.negative_control:
            original = source.read_text(encoding="latin-1")
            changed = original.replace("sizeof(mgMaterial)", "sizeof(int)", 1)
            if changed == original:
                raise ValueError("negative control could not find sizeof(mgMaterial)")
            with tempfile.TemporaryDirectory(prefix="wp11-") as temp:
                mutant = Path(temp) / source.name
                mutant.write_text(changed, encoding="latin-1")
                mutant_ast = ast_for(mutant, args.compile_commands.resolve(), args.clang, source)
            mutant_ok, mutant_message = check_ast(mutant_ast)
            if mutant_ok:
                print("runtime contracts: ERROR negative control escaped: " + mutant_message,
                      file=sys.stderr)
                return 1
            print(f"runtime contracts: negative control rejected: {mutant_message}")
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"runtime contracts: ERROR: {exc}", file=sys.stderr)
        return 2
    print(f"runtime contracts: {message}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
