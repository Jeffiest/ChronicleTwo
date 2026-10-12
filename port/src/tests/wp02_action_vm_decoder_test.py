#!/usr/bin/env python3
"""Decoder controls for scripts/port/wp02_action_vm.py: retail c01.stb decodes, malformed images are refused.

    wp02_action_vm_decoder_test.py <data-root>      exit 0 pass, 77 when c01.stb is absent
"""

import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / "scripts" / "port"))
import wp02_action_vm as vm  # noqa: E402

DATA = pathlib.Path(sys.argv.pop(1)) if len(sys.argv) > 1 else None
PROGRAM = DATA / "dungeon" / "act_script" / "c01.stb" if DATA else None


@unittest.skipUnless(PROGRAM and PROGRAM.exists(), "c01.stb not found")
class RealProgram(unittest.TestCase):
    def setUp(self):
        self.image = PROGRAM.read_bytes()

    def test_programs_and_entry_points(self):
        program = vm.Program(self.image)
        self.assertEqual(program.version, 2)
        numbers = sorted(no for no, _ in program.programs())
        self.assertEqual(numbers, [100, 150, 200, 500, 550, 600, 700, 1400])

    def test_get_btn_shape(self):
        program = vm.Program(self.image)
        sites = list(vm.ext_sites(program, {43}))
        self.assertGreaterEqual(len(sites), 10)
        for _, number, argc, operands in sites:
            self.assertEqual((number, argc, [o[0] for o in operands]), (43, 2, ["PUSH_CONST", "LOAD_ADDR"]))

    def test_truncated_image_is_refused(self):
        for keep in (0, 8, 0x3F, 0x40, 0x100, len(self.image) // 2):
            with self.assertRaises(ValueError, msg="keep=%d" % keep):
                program = vm.Program(self.image[:keep])
                list(vm.ext_sites(program, set()))

    def test_bad_magic_is_refused(self):
        with self.assertRaises(ValueError):
            vm.Program(b"XXXX" + self.image[4:])

    def test_offsets_outside_the_image_are_refused(self):
        header = bytearray(self.image)
        struct.pack_into("<i", header, 4, len(self.image) + 16)  # main
        with self.assertRaises(ValueError):
            vm.Program(bytes(header))
        header = bytearray(self.image)
        struct.pack_into("<i", header, 16, 0x7FFFFFF)  # prog_num
        with self.assertRaises(ValueError):
            vm.Program(bytes(header))


if __name__ == "__main__":
    if PROGRAM is None or not PROGRAM.exists():
        print("SKIP: c01.stb not found")
        sys.exit(77)
    unittest.main()
