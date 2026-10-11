"""Read each unit's compiler options from the CMake toolchain the way the build does."""

from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import draft_check


TOOLCHAIN = '''set(CC_FLAGS -O3,p -strings readonly -c)
set(UNIT_CC_FLAGS_alpha -O2,p)
set(UNIT_CC_FLAGS_alphabet -O0)
set(UNIT_CC_FLAGS_beta -inline off)
'''


class UnitFlagTests(unittest.TestCase):
    def read_flags(self, unit):
        with tempfile.TemporaryDirectory() as directory:
            toolchain = Path(directory) / 'Toolchain.cmake'
            toolchain.write_text(TOOLCHAIN)
            with patch.object(draft_check, 'TOOLCHAIN', toolchain):
                return draft_check.unit_cc_flags(unit)

    def test_splits_the_unit_line_on_whitespace(self):
        self.assertEqual(self.read_flags('beta'), ['-inline', 'off'])

    def test_matches_whole_unit_names_only(self):
        self.assertEqual(self.read_flags('alpha'), ['-O2,p'])
        self.assertEqual(self.read_flags('alphabet'), ['-O0'])

    def test_unit_without_a_line_has_no_flags(self):
        self.assertEqual(self.read_flags('gamma'), [])

    def test_ignores_the_common_flags_line(self):
        self.assertEqual(self.read_flags('CC'), [])


if __name__ == '__main__':
    unittest.main()
