"""Negative-control fixtures for the batch-05 active build contract reporter."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from check_active_contracts import analyze


class ActiveContractsBatch05Tests(unittest.TestCase):
    def fixture(self, stale=False):
        temp = tempfile.TemporaryDirectory()
        root = Path(temp.name)
        for path in ("port/cmake", "port/src/tests", "port/src", "scripts/port", "build"):
            (root / path).mkdir(parents=True, exist_ok=True)
        (root / "port/cmake/NativeOwnership.cmake").write_text(
            'set(CT_NATIVE_SUPERSEDED_SOURCES\n "${PORT_DIR}/stub.cpp"\n)\n', encoding="utf-8"
        )
        (root / "port/src/stub.cpp").write_text("PORT_OVERRIDE void Stub() {}\n", encoding="utf-8")
        (root / "port/src/real.cpp").write_text("PORT_OVERRIDE void Real() {}\n", encoding="utf-8")
        (root / "port/src/tests/CMakeLists.txt").write_text(
            "set(TEST_SOURCES\n ${CMAKE_CURRENT_SOURCE_DIR}/selected_test.cpp)\n", encoding="utf-8"
        )
        (root / "port/src/tests/selected_test.cpp").write_text(
            "TEST(RealPath, Executes) { ASSERT_TRUE(true); }\n", encoding="utf-8"
        )
        (root / "port/cmake/CompatibilityTargets.cmake").write_text(
            "add_executable(ct_demo_test EXCLUDE_FROM_ALL ${CT_TESTS}/demo.cpp)\n", encoding="utf-8"
        )
        build = root / "build"
        compile_files = [root / "port/src/real.cpp", root / "port/src/tests/selected_test.cpp"]
        if stale:
            compile_files.append(root / "port/src/stub.cpp")
        (build / "compile_commands.json").write_text(json.dumps([
            {"directory": str(root), "file": str(path), "command": "c++ -c"}
            for path in compile_files
        ]), encoding="utf-8")
        (build / "port_overrides.txt").write_text("Real()\nStub()\n", encoding="utf-8")
        collector = [root / "port/src/real.cpp"] + ([root / "port/src/stub.cpp"] if stale else [])
        (build / "port_override_sources.txt").write_text("\n".join(map(str, collector)), encoding="utf-8")
        return temp, root, build

    def test_clean_active_and_excluded_control(self):
        temp, root, build = self.fixture()
        self.addCleanup(temp.cleanup)
        report = analyze(root, build)
        self.assertEqual(report["compiled_native_port_translation_units"], ["port/src/real.cpp"])
        self.assertEqual(report["superseded_sources_compiled"], [])
        self.assertEqual(report["superseded_sources_still_collected"], [])
        self.assertEqual(report["selected_gtest_sources_compiled"], ["selected_test.cpp"])
        self.assertEqual(report["selected_gtest_case_names"], ["RealPath.Executes"])
        self.assertIn("unknown", report["reachability"])

    def test_superseded_unit_is_caught_when_collected_and_compiled(self):
        temp, root, build = self.fixture(stale=True)
        self.addCleanup(temp.cleanup)
        report = analyze(root, build)
        self.assertEqual(report["superseded_sources_compiled"], ["stub.cpp"])
        self.assertEqual(report["superseded_sources_still_collected"], ["stub.cpp"])

    def test_reference_target_does_not_hide_a_production_leak(self):
        temp, root, build = self.fixture()
        self.addCleanup(temp.cleanup)
        database = build / "compile_commands.json"
        commands = json.loads(database.read_text(encoding="utf-8"))
        commands.append({"file": str(root / "port/src/stub.cpp"),
                         "command": "c++ -o CMakeFiles/ct_drawprim_reference_tests.dir/stub.cpp.obj -c"})
        database.write_text(json.dumps(commands), encoding="utf-8")
        self.assertEqual(analyze(root, build)["superseded_sources_compiled"], [])
        commands.append({"file": str(root / "port/src/stub.cpp"),
                         "command": "c++ -o CMakeFiles/ct_port.dir/stub.cpp.obj -c"})
        database.write_text(json.dumps(commands), encoding="utf-8")
        self.assertEqual(analyze(root, build)["superseded_sources_compiled"], ["stub.cpp"])


if __name__ == "__main__":
    unittest.main()
