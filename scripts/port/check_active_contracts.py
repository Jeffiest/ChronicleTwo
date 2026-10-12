"""Report build-active native ownership and regression coverage from CMake artifacts.

This is an artifact consistency check, not a C++ compiler, linker symbol map, or
call-graph analysis. "Compiled" means a translation unit occurs in the selected
build's compile_commands.json; reachability is always reported as unknown.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


def norm(path: str | Path) -> str:
    return str(Path(path).resolve()).replace("\\", "/").casefold()


def cmake_list(path: Path, variable: str) -> list[str]:
    text = path.read_text(encoding="utf-8")
    match = re.search(rf"set\({re.escape(variable)}\s*(.*?)\n\)", text, re.S)
    if not match:
        raise ValueError(f"missing CMake set({variable}) in {path}")
    values = []
    for native_path, literal_path in re.findall(
        r'"?\$\{PORT_DIR\}/([^"\s)]+)|"([^"]+)"', match.group(1)
    ):
        values.append(native_path or literal_path)
    return values


def analyze(repo: Path, build: Path) -> dict:
    db_path = build / "compile_commands.json"
    overrides_path = build / "port_overrides.txt"
    sources_path = build / "port_override_sources.txt"
    for path in (db_path, overrides_path, sources_path):
        if not path.is_file():
            raise ValueError(f"required configured-build artifact missing: {path}")
    commands = json.loads(db_path.read_text(encoding="utf-8"))
    compiled = {norm(entry["file"]) for entry in commands}
    # This target checks preserved GS packets, independently of the native renderer.
    reference_marker = "CMakeFiles/ct_drawprim_reference_tests.dir/"
    reference_commands = [entry for entry in commands if reference_marker in
                          str(entry.get("output", entry.get("command", ""))).replace("\\", "/")]
    production_compiled = {norm(entry["file"]) for entry in commands if entry not in reference_commands}
    ownership_file = repo / "port/cmake/NativeOwnership.cmake"
    superseded = cmake_list(ownership_file, "CT_NATIVE_SUPERSEDED_SOURCES")
    source_lines = sources_path.read_text(encoding="utf-8").splitlines()
    collected = {norm(line) for line in source_lines if line.strip()}
    overrides = [line.strip() for line in overrides_path.read_text(encoding="utf-8").splitlines() if line.strip()]
    native_sources = sorted(
        Path(line).relative_to(repo).as_posix() for line in source_lines
        if line.strip() and norm(line).startswith(norm(repo / "port/src")) and norm(line) in compiled
    )
    compiled_definitions = set()
    for rel in native_sources:
        text = (repo / rel).read_text(encoding="utf-8", errors="replace")
        compiled_definitions.update(re.findall(r"\bPORT_OVERRIDE\s+[^;{}]*?\b([A-Za-z_]\w*(?:::\w+)?(?:\s*\([^;{}]*?\))?)\s*\{", text))
    override_set = set(overrides)
    excluded_but_collected = [p for p in superseded if norm(repo / "port/src" / p) in collected]
    excluded_but_compiled = [p for p in superseded if norm(repo / "port/src" / p) in production_compiled]

    # The CMake file is authoritative for the current game-specific GTest source selection.
    test_cmake = (repo / "port/src/tests/CMakeLists.txt").read_text(encoding="utf-8")
    selected_block = re.search(r"set\(TEST_SOURCES(.*?)\)", test_cmake, re.S)
    selected = re.findall(r"([\w]+_test\.cpp)", selected_block.group(1)) if selected_block else []
    selected_test_sources = [f"port/src/tests/{name}" for name in selected]
    test_sources = sorted({Path(e["file"]).name for e in commands if "/port/src/tests/" in norm(e["file"])})
    active_gtest = sorted(set(selected) & set(test_sources))
    compat_cmake = (repo / "port/cmake/CompatibilityTargets.cmake").read_text(encoding="utf-8")
    optin = re.findall(r"add_executable\(ct_(\w+)_test\s+EXCLUDE_FROM_ALL\s+\$\{CT_TESTS\}/([^ )]+)",
                       compat_cmake)
    loop = re.search(r"foreach\(test_name IN ITEMS ([^)]+)\)", compat_cmake)
    standalone_map = {
        "script_storage": "script_storage.cpp",
        "monster_memory": "monster_memory_standalone.cpp",
        "input_lifecycle": "input_lifecycle_test.cpp",
        "color_capture": "color_capture_standalone.cpp",
        "photo_name_storage": "photo_name_storage_standalone.cpp",
    }
    if loop:
        optin.extend((name, standalone_map[name]) for name in loop.group(1).split() if name in standalone_map)
    gtest_names = []
    for name in active_gtest:
        text = (repo / "port/src/tests" / name).read_text(encoding="utf-8", errors="replace")
        gtest_names.extend(re.findall(r"\bTEST(?:_F|_P)?\s*\(\s*([\w]+)\s*,\s*([\w]+)", text))
    placeholders = []
    for rel in native_sources:
        text = (repo / rel).read_text(encoding="utf-8", errors="replace")
        if "PS2_UNIMPLEMENTED()" in text or re.search(r"abort\s*\(\s*\)", text):
            placeholders.append(rel)
    return {
        "build_dir": str(build),
        "compile_database_entries": len(commands),
        "compiled_native_port_translation_units": native_sources,
        "compiled_native_port_count": len(native_sources),
        "override_artifact": str(overrides_path),
        "override_names_in_generated_list_count": len(overrides),
        "override_definition_source_collector_count": len(collected),
        "generated_override_names_with_matching_compiled_definition": sorted(
            name for name in override_set if any(name.split("(", 1)[0] in item for item in compiled_definitions)
        ),
        "superseded_source_count": len(superseded),
        "superseded_sources_still_collected": excluded_but_collected,
        "superseded_sources_compiled": excluded_but_compiled,
        "isolated_reference_translation_units": sorted({norm(entry["file"]) for entry in reference_commands}),
        "compiled_translation_units_with_obvious_abort_placeholder": placeholders,
        "selected_gtest_sources": selected_test_sources,
        "selected_gtest_sources_compiled": active_gtest,
        "all_compiled_test_translation_unit_basenames": test_sources,
        "selected_gtest_case_names": [".".join(pair) for pair in sorted(gtest_names)],
        "opt_in_standalone_target_sources": [
            {"target": f"ct_{target}_test", "source": f"port/src/tests/{source}"}
            for target, source in optin
        ],
        "reachability": "unknown: compile database and override list do not prove linked call paths",
        "limitations": [
            "Compile database entries establish translation-unit compilation only.",
            "Override names come from generated port_overrides.txt; no linker map or overload identity is checked.",
            "Text search for obvious abort placeholders is a warning signal, not behavioral classification.",
            "GTest registration is source-derived; CTest discovery/runtime execution must be checked separately.",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--check", action="store_true", help="fail on ownership/source-selection contradictions")
    parser.add_argument("--json", action="store_true", help="print the complete deterministic manifest")
    args = parser.parse_args()
    try:
        report = analyze(args.repo.resolve(), args.build_dir.resolve())
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"active_contracts: error: {exc}", file=sys.stderr)
        return 2
    problems = []
    if report["superseded_sources_still_collected"]:
        problems.append("superseded native units remain in override source collector")
    if report["superseded_sources_compiled"]:
        problems.append("superseded native units are compiled")
    if args.check and problems:
        print("active_contracts: " + "; ".join(problems), file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(f"compiled native units: {report['compiled_native_port_count']}; generated override names: "
              f"{report['override_names_in_generated_list_count']}; source collector units: "
              f"{report['override_definition_source_collector_count']}")
        print(f"excluded superseded units: {report['superseded_source_count']}; compiled: "
              f"{len(report['superseded_sources_compiled'])}; collector leaks: "
              f"{len(report['superseded_sources_still_collected'])}")
        print(f"compiled selected GTests: {', '.join(report['selected_gtest_sources_compiled'])}; "
              f"cases: {len(report['selected_gtest_case_names'])}")
        print("reachability: unknown")
        print("Use --json for source-level manifest, override evidence and placeholder warnings.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
