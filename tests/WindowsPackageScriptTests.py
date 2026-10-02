"""Portable checks for Windows archive creation; no Windows build tools needed."""
import hashlib
import importlib.util
import os
from pathlib import Path
import stat
import sys
import tempfile
import unittest
import zipfile


SCRIPT = Path(__file__).resolve().parents[1] / 'distribute/package_windows.py'
SPEC = importlib.util.spec_from_file_location('package_windows', SCRIPT)
PACKAGE = importlib.util.module_from_spec(SPEC)
previous_bytecode = sys.dont_write_bytecode
sys.dont_write_bytecode = True
try:
    SPEC.loader.exec_module(PACKAGE)
finally:
    sys.dont_write_bytecode = previous_bytecode


class WindowsPackageScriptTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='juicy16-package-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'Source'
        self.root.mkdir()
        (self.root / 'tools').mkdir()
        self.script = self.root / 'tools/ci_gates.sh'
        self.script.write_text('#!/bin/bash\nexit 0\n', encoding='utf-8')
        self.script.chmod(0o644)  # Emulate a Windows checkout without Unix modes.
        (self.root / 'README.md').write_text('Source instructions\n', encoding='utf-8')
        self.output = Path(self.temp.name) / 'Source.zip'

    def test_git_executable_modes_survive_windows_source_packaging(self):
        PACKAGE.archive_tree(self.root, self.output, {'tools/ci_gates.sh'})
        with zipfile.ZipFile(self.output) as archive:
            executable = archive.getinfo('Source/tools/ci_gates.sh')
            document = archive.getinfo('Source/README.md')
            self.assertEqual(executable.create_system, 3)
            self.assertEqual(stat.S_IMODE(executable.external_attr >> 16), 0o755)
            self.assertEqual(stat.S_IMODE(document.external_attr >> 16), 0o644)
            self.assertEqual(archive.read(executable), self.script.read_bytes())

    @unittest.skipIf(os.name == 'nt', 'Windows chmod does not provide Unix executable bits')
    def test_native_executable_mode_is_preserved(self):
        self.script.chmod(0o755)
        PACKAGE.archive_tree(self.root, self.output)
        with zipfile.ZipFile(self.output) as archive:
            self.assertEqual(stat.S_IMODE(archive.getinfo('Source/tools/ci_gates.sh').external_attr >> 16), 0o755)

    def test_archive_and_checksum_remain_deterministic(self):
        PACKAGE.archive_tree(self.root, self.output, {'tools/ci_gates.sh'})
        first = self.output.read_bytes()
        PACKAGE.archive_tree(self.root, self.output, {'tools/ci_gates.sh'})
        self.assertEqual(self.output.read_bytes(), first)
        self.assertEqual(self.output.with_suffix('.zip.sha256').read_text(encoding='utf-8'),
                         hashlib.sha256(first).hexdigest() + '  Source.zip\n')
        with zipfile.ZipFile(self.output) as archive:
            self.assertIsNone(archive.testzip())
            self.assertTrue(all(info.date_time == (2026, 1, 1, 0, 0, 0) for info in archive.infolist()))

    def recipe(self, hashes):
        recipe = Path(self.temp.name) / 'recipe.ps1'
        recipe.write_text(''.join(
            f"Get-PinnedSource -Name '{name}' `\n"
            f"    -Url 'https://example.invalid/{name}.tar.gz' `\n"
            f"    -ExpectedSha256 '{digest}'\n" for name, digest in hashes.items()), encoding='utf-8')
        return recipe

    def test_source_archive_closure_is_complete_and_checksummed(self):
        archive = self.root / 'fluidsynth.tar.gz'
        archive.write_bytes(b'Pinned upstream source archive')
        recipe = self.recipe({'fluidsynth': PACKAGE.sha(archive)})
        self.assertEqual(PACKAGE.validated_dependency_archives(recipe, self.root), [archive])
        archive.write_bytes(b'Wrong source revision')
        with self.assertRaisesRegex(RuntimeError, 'checksum mismatch'):
            PACKAGE.validated_dependency_archives(recipe, self.root)
        archive.unlink()
        with self.assertRaisesRegex(RuntimeError, 'Missing corresponding-source'):
            PACKAGE.validated_dependency_archives(recipe, self.root)

    def test_source_recipe_parse_cannot_succeed_empty_or_partial(self):
        recipe = self.recipe({})
        with self.assertRaisesRegex(RuntimeError, 'Cannot read every pinned'):
            PACKAGE.validated_dependency_archives(recipe, self.root)
        recipe.write_text("Get-PinnedSource -Name 'fluidsynth' `\n -ExpectedSha256 'bad'\n", encoding='utf-8')
        with self.assertRaisesRegex(RuntimeError, 'Cannot read every pinned'):
            PACKAGE.validated_dependency_archives(recipe, self.root)

    def test_current_recipe_requires_pinned_archives(self):
        recipe = SCRIPT.parents[1] / 'tools/build_windows_dependencies.ps1'
        with self.assertRaisesRegex(RuntimeError, 'Missing corresponding-source archive:.*fluidsynth.tar.gz'):
            PACKAGE.validated_dependency_archives(recipe, self.root)


if __name__ == '__main__':
    unittest.main()
