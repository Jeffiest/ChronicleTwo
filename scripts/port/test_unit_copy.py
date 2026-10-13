import tempfile
import unittest
from pathlib import Path

import unit_copy


class RetailBridgeTests(unittest.TestCase):
    def setUp(self):
        root = Path(__file__).resolve().parents[2]
        self.override = unit_copy.load_port_override(root / "tools/kazarov/scripts/build/port_override.py")

    def test_bridge_preserves_body_and_public_calls(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "dng_main.cpp"
            body = "void ResetEyeView(CActionChara *chara) { ResetEyeView(chara); chara->Reset(); }"
            source.write_text(body + "\nvoid Caller(CActionChara *chara) { ResetEyeView(chara); }")
            mirror = unit_copy.retail_bridges(self.override, source, True)
            self.assertEqual(mirror, f'#line 1 "{source.resolve().as_posix()}"\n' +
                             body.replace("void ResetEyeView(", "void PortRetailResetEyeView(", 1))
            stripped = self.override.strip_unit(source, {"ResetEyeView(CActionChara*)"}, {}, (), True)
            self.assertIn("void Caller(CActionChara *chara) { ResetEyeView(chara); }", stripped)
            self.assertNotIn(body, stripped)

    def test_bridge_maps_to_the_retail_definition_line(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "dng_main.cpp"
            source.write_text("// retail declarations\n\n\nvoid ResetEyeView(CActionChara *chara) {\n    chara->Reset();\n}")
            mirror = unit_copy.retail_bridges(self.override, source, True)
            self.assertTrue(mirror.startswith(f'#line 4 "{source.resolve().as_posix()}"\n'))
            self.assertIn("void PortRetailResetEyeView(CActionChara *chara) {\n    chara->Reset();\n}", mirror.replace("\r\n", "\n"))

    def test_missing_retail_body_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "dng_main.cpp"
            source.write_text("void Other() {}")
            with self.assertRaises(self.override.Error):
                unit_copy.retail_bridges(self.override, source, True)

    def test_other_units_have_no_bridge(self):
        self.assertEqual(unit_copy.retail_bridges(self.override, Path("other.cpp"), True), "")


if __name__ == "__main__":
    unittest.main()
