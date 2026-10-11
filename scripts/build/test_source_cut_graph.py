"""Explicit address cuts must invalidate the split and every dependent unit."""

import unittest

from test_objdiff_graph import ObjdiffGraphFixture


class SourceCutGraphTests(ObjdiffGraphFixture):
    def setUp(self):
        super().setUp()
        self.write('split.py', '''import pathlib,re,sys
cuts=sorted({name for source in pathlib.Path('src').glob('*.cpp')
             for name in re.findall(r'\\bD_[0-9A-F]{8}\\b', source.read_text())})
pathlib.Path(sys.argv[1]).write_text('\\n'.join(cuts))
''')
        self.write('assemble.py', '''import pathlib,sys
output=pathlib.Path(sys.argv[sys.argv.index('-o')+1])
output.write_bytes(pathlib.Path('out/split').read_bytes())
''')
        cmake = (self.root / 'CMakeLists.txt').read_text()
        cmake = cmake.replace('COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_SOURCE_DIR}/${SPLIT_STAMP}"',
                              'COMMAND "${PYTHON}" "${CMAKE_SOURCE_DIR}/split.py" "${CMAKE_SOURCE_DIR}/${SPLIT_STAMP}" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"')
        self.write('CMakeLists.txt', cmake)
        self.run_command('cmake', '-S', '.', '-B', 'out', '-G', 'Ninja')

    def test_ordinary_edit_rebuilds_only_its_base(self):
        self.write('src/first.cpp', 'void changed_function() {}\n')
        self.build()
        split = self.root / 'out/split'
        other = self.root / 'out/objdiff/target/second.s.o'
        split_time, other_time = split.stat().st_mtime_ns, other.stat().st_mtime_ns
        self.touch('src/first.cpp')
        self.write('src/first.cpp', 'void changed_function() { return; }\n')
        output = self.build()
        self.assertIn('CC (objdiff base) src/first.cpp', output)
        self.assertNotIn('Splitting fixture', output)
        self.assertNotIn('AS (objdiff target)', output)
        self.assertEqual(split.stat().st_mtime_ns, split_time)
        self.assertEqual(other.stat().st_mtime_ns, other_time)

    def test_added_and_removed_cuts_rebuild_other_units_like_a_clean_build(self):
        self.build()
        other = self.root / 'out/objdiff/target/second.s.o'
        self.assertEqual(other.read_bytes(), b'')
        for source, expected in (('int D_0000300C;', b'D_0000300C'), ('', b'')):
            with self.subTest(source=source):
                self.touch('src/first.cpp')
                self.write('src/first.cpp', source)
                self.assertIn('Splitting fixture', self.pending())
                self.build()
                incremental = other.read_bytes()
                self.assertEqual(incremental, expected)
                self.run_command('ninja', '-C', 'out', '-t', 'clean')
                self.build()
                self.assertEqual(other.read_bytes(), incremental)


class SourceClassificationTests(unittest.TestCase):
    def signature(self, text):
        from source_cuts import source_split_signature
        return source_split_signature(text)

    def test_source_classifier_tracks_source_definitions(self):
        self.assertEqual(self.signature('void first() {}\nstatic int second(int arg) { return arg; }\n'),
                         ['first', 'second'])

    def test_comment_edits_do_not_change_the_signature(self):
        self.assertEqual(self.signature('void first() {} /* note */\n// line comment\n'),
                         self.signature('void first() {}\n'))


if __name__ == '__main__':
    unittest.main()
