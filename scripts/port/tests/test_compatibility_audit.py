import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "compatibility_audit.py"


class CompatibilityAuditTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.retail = self.root / "ps2"
        self.host = self.root / "port"
        self.out = self.root / "docs" / "COMPATIBILITY_AUDIT.md"
        self.ledger = self.root / "docs" / "compatibility_decisions.tsv"
        (self.retail / "src").mkdir(parents=True)
        (self.host / "src").mkdir(parents=True)
        (self.retail / "src" / "event.cpp").write_text(
            """void SetEventScript(mgCMemory *memory) {
    RS_STACKDATA *stack_data = (RS_STACKDATA *) memory->Alloc(0x40);
    RS_CALLDATA *call_data = (RS_CALLDATA *) memory->Alloc(0x180);
}
int SetMonsterScript(mgCMemory *memory) {
    RS_STACKDATA *value_stack = (RS_STACKDATA *) memory->Alloc(0x40);
    RS_CALLDATA *call_stack = (RS_CALLDATA *) memory->Alloc(0x180);
}
int SetActionScript(mgCMemory *memory) {
    RS_STACKDATA *stack = reinterpret_cast<RS_STACKDATA *>(memory->Alloc(0x40));
}
""",
            encoding="utf-8",
        )
        (self.retail / "src" / "scene.cpp").write_text(
            "void CScene::ClearStack() { *(int *)((char *)this + 0x18) = 0; }\n",
            encoding="utf-8",
        )
        (self.retail / "src" / "dng_event.cpp").write_text(
            """void LoadMonsterFile() {
    u8 *monster_man = (u8 *) ActiveMonster;
    slot = (mgCMemory *) (monster_man + offset + 4);
}
""",
            encoding="utf-8",
        )
        (self.host / "src" / "scene.cpp").write_text(
            "PORT_OVERRIDE void CScene::ClearStack() { stack[0] = 0; }\n",
            encoding="utf-8",
        )

    def tearDown(self):
        self.temp.cleanup()

    def command(self, *args):
        return subprocess.run(
            [sys.executable, str(SCRIPT), "--source-root", str(self.retail), "--host-root", str(self.host),
             "--inventory", str(self.out), "--decisions", str(self.ledger), *args],
            text=True, capture_output=True,
        )

    def test_known_cast_allocations_and_override_identity_are_in_inventory(self):
        result = self.command("--write")
        self.assertEqual(result.returncode, 0, result.stderr)
        text = self.out.read_text(encoding="utf-8")
        self.assertIn("SetEventScript", text)
        self.assertIn("SetMonsterScript", text)
        self.assertIn("SetActionScript", text)
        self.assertIn("reinterpret_cast", text)
        self.assertIn("0x40", text)
        self.assertIn("0x180", text)
        self.assertIn("retail/src/scene.cpp", text)
        self.assertIn("| src/scene.cpp |", text)
        self.assertIn("monster_man + offset", text)
        self.assertEqual(self.command("--check").returncode, 0)

    def test_new_site_fails_check_until_inventory_and_ledger_are_reviewed(self):
        self.assertEqual(self.command("--write").returncode, 0)
        with (self.retail / "src" / "event.cpp").open("a", encoding="utf-8") as f:
            f.write("void Added(mgCMemory *memory) { X *x = (X *) memory->Alloc(0x20); }\n")
        result = self.command("--check")
        self.assertEqual(result.returncode, 1)
        self.assertIn("stale inventory", result.stderr)
        self.assertIn("missing from decision ledger", result.stderr)

    def test_output_is_stable(self):
        self.assertEqual(self.command("--write").returncode, 0)
        first_inventory = self.out.read_bytes()
        first_ledger = self.ledger.read_bytes()
        self.assertEqual(self.command("--write").returncode, 0)
        self.assertEqual(first_inventory, self.out.read_bytes())
        self.assertEqual(first_ledger, self.ledger.read_bytes())

    def test_byte_stride_pointer_arrays_are_inventoried(self):
        (self.retail / "src" / "menu.cpp").write_text(
            "void Menu(char **names, PART **row, int i, int offset) {\n"
            "    Load(*(char **) ((u8 *) names + i));\n"
            "    Enable(*(PART **) ((u8 *) row + offset));\n"
            "    Enable(row[i]);\n}\n", encoding="utf-8")
        self.assertEqual(self.command("--write").returncode, 0)
        text = self.out.read_text(encoding="utf-8")
        self.assertEqual(text.count("| byte-stride-pointer-array |"), 2)
        self.assertIn("names + i", text)
        self.assertIn("row + offset", text)
        self.assertNotIn("Enable(row[i])", text)


if __name__ == "__main__":
    unittest.main()
