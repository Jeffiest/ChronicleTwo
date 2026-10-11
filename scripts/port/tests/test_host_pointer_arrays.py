import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from check_host_pointer_arrays import findings


class HostPointerArraysTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.generated = self.root / "generated"
        self.host = self.root / "host"
        self.generated.mkdir()
        self.host.mkdir()
        self.unit = self.generated / "menu.cpp"
        self.unit.write_text("void Step() {}\n")

    def tearDown(self):
        self.temp.cleanup()

    def test_generated_pointer_access_fails_even_without_inventory_change(self):
        self.unit.write_text("value = *(PART **) ((u8 *) row + offset);\n")
        self.assertEqual(len(findings(self.generated, self.host)), 1)

    def test_native_pointer_access_fails(self):
        (self.host / "menu.cpp").write_text("value = *(char **) ((u8 *) names + i);\n")
        self.assertEqual(len(findings(self.generated, self.host)), 1)

    def test_native_indexing_passes_and_negative_control_fixtures_are_excluded(self):
        self.unit.write_text("value = row[i];\n")
        tests = self.host / "tests"
        tests.mkdir()
        (tests / "negative.cpp").write_text("value = *(PART **) ((u8 *) row + offset);\n")
        self.assertEqual(findings(self.generated, self.host), [])

    def test_empty_generated_tree_is_an_error(self):
        self.unit.unlink()
        with self.assertRaisesRegex(ValueError, "no generated host"):
            findings(self.generated, self.host)


if __name__ == "__main__":
    unittest.main()
