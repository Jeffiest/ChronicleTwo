"""Check that setup_mwccgap applies the submodule ELF patch idempotently."""
from pathlib import Path
import sys
import subprocess
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/mwccgap'))

import setup_mwccgap


def test_patch_setup():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        module = root / 'tools/mwccgap'
        (module / 'mwccgap').mkdir(parents=True)
        (module / 'mwccgap.py').touch()
        paths = ('mwccgap/elf.py', 'mwccgap/mwccgap.py')
        for path in paths:
            original = subprocess.check_output(
                ['git', '-C', str(ROOT / 'tools/mwccgap'), 'show', 'HEAD:' + path])
            (module / path).write_bytes(original)
        (module / 'mwccgap/mwccgap.py').chmod(0o755)
        subprocess.run(['git', 'init', '-q', str(module)], check=True)
        subprocess.run(['git', '-C', str(module), 'add', '.'], check=True)
        patches = root / 'scripts/build/patches'
        patches.mkdir(parents=True)
        shutil.copy2(ROOT / 'scripts/build/patches/mwccgap-elf.patch', patches / 'mwccgap-elf.patch')
        saved_root = setup_mwccgap.ROOT
        setup_mwccgap.ROOT = root
        try:
            # Fresh setup, then the already-prepared ELF-only state.
            setup_mwccgap.apply_patch('mwccgap-elf.patch')
            elf_bytes = (module / paths[0]).read_bytes()
            setup_mwccgap.apply_patch('mwccgap-elf.patch')
            assert (module / paths[0]).read_bytes() == elf_bytes
            fixed_bytes = [(module / path).read_bytes() for path in paths]
            # Repeated setup leaves the patched files unchanged.
            setup_mwccgap.apply_patch('mwccgap-elf.patch')
            assert [(module / path).read_bytes() for path in paths] == fixed_bytes
        finally:
            setup_mwccgap.ROOT = saved_root


if __name__ == '__main__':
    test_patch_setup()
    print('mwccgap ELF patch checks passed')
