"""Execute the macOS installer in an isolated home with mocked platform commands.

Checksums and file operations remain real. Mocked codesign/lipo only exercise
installer decisions; actual distributable signing/architecture gates run separately.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


INSTALLER = Path(__file__).resolve().parents[1] / 'distribute/install_macos.command'
FORMATS = {
    'au': ('AU/Juicy16.component', 'Library/Audio/Plug-Ins/Components/Juicy16.component'),
    'vst3': ('VST3/Juicy16.vst3', 'Library/Audio/Plug-Ins/VST3/Juicy16.vst3'),
    'app': ('Standalone/Juicy16.app', 'Applications/Juicy16.app'),
}
STUB = r'''#!/usr/bin/env python3
import json, os
from pathlib import Path
import shutil, sys
command = Path(sys.argv[0]).name
args = sys.argv[1:]
with open(os.environ['JUICY16_INSTALL_TEST_LOG'], 'a', encoding='utf-8') as log:
    log.write(json.dumps([command, *args]) + '\n')
if command == 'uname':
    print(os.environ.get('JUICY16_TEST_HOST_ARCH', 'arm64') if args == ['-m'] else 'Darwin')
elif command == 'lipo':
    print(os.environ.get('JUICY16_TEST_BUNDLE_ARCH', 'arm64'))
elif command == 'codesign':
    path = args[-1]
    staged = '.juicy16-install.' in path
    packaged = str(Path(os.environ['JUICY16_INSTALL_TEST_PACKAGE'])) in path
    if os.environ.get('JUICY16_TEST_FAIL_SOURCE') == '1' and packaged:
        sys.exit(1)
    if os.environ.get('JUICY16_TEST_FAIL_STAGE') == '1' and staged:
        sys.exit(1)
    if os.environ.get('JUICY16_TEST_FAIL_FINAL') == '1' and not staged and not packaged:
        sys.exit(1)
elif command == 'ditto':
    if os.environ.get('JUICY16_TEST_FAIL_COPY') == '1' and '.juicy16-install.' in args[1]:
        sys.exit(1)
    shutil.copytree(args[0], args[1], symlinks=True, dirs_exist_ok=True)
elif command == 'xattr':
    pass
elif command == 'tput':
    pass
else:
    sys.exit('Unexpected mock command: ' + command)
'''


@unittest.skipUnless(shutil.which('bash') and shutil.which('shasum'),
                     'installer behavioral tests require bash and shasum')
class MacInstallerScriptTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='juicy16-install-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.package = self.root / 'package with spaces'
        self.home = self.root / 'home with spaces'
        self.bin = self.root / 'mock-bin'
        for directory in (self.package, self.home, self.bin):
            directory.mkdir()
        self.script = self.package / 'install_macos.command'
        shutil.copy2(INSTALLER, self.script)
        self.log = self.root / 'commands.jsonl'
        self.log.write_text('', encoding='utf-8')
        for command in ('uname', 'lipo', 'codesign', 'ditto', 'xattr', 'tput'):
            stub = self.bin / command
            stub.write_text(STUB, encoding='utf-8')
            stub.chmod(0o755)
        self.env = dict(os.environ, HOME=str(self.home),
                        PATH=str(self.bin) + os.pathsep + os.environ['PATH'],
                        JUICY16_INSTALL_TEST_LOG=str(self.log),
                        JUICY16_INSTALL_TEST_PACKAGE=str(self.package))
        # The child alone gets this temporary HOME; the real user home is never
        # read or modified by the installer under test.
        for key, (source, _) in FORMATS.items():
            self.bundle(self.package / source, (key + '-new').encode())
        self.manifest()

    @staticmethod
    def bundle(path, payload):
        binary = path / 'Contents/MacOS/Juicy16'
        binary.parent.mkdir(parents=True)
        binary.write_bytes(payload)
        binary.chmod(0o755)
        (path / 'Contents/Info.plist').write_text('<plist><dict/></plist>\n', encoding='utf-8')

    def manifest(self):
        files = sorted(path for path in self.package.rglob('*')
                       if path.is_file() and path.name != 'SHA256SUMS')
        (self.package / 'SHA256SUMS').write_text(''.join(
            hashlib.sha256(path.read_bytes()).hexdigest() + '  '
            + path.relative_to(self.package).as_posix() + '\n' for path in files), encoding='utf-8')

    def run_installer(self, choice='', **overrides):
        env = dict(self.env, **overrides)
        result = subprocess.run([shutil.which('bash'), str(self.script)], input=choice + '\n\n',
                                text=True, capture_output=True, env=env, timeout=20)
        self.assertFalse(list(self.home.rglob('.juicy16-install.*')), result.stdout + result.stderr)
        return result

    def commands(self):
        return [json.loads(line) for line in self.log.read_text(encoding='utf-8').splitlines()]

    def assert_selection(self, selected):
        for key, (_, target) in FORMATS.items():
            installed = self.home / target
            if key in selected:
                self.assertEqual((installed / 'Contents/MacOS/Juicy16').read_bytes(), (key + '-new').encode())
                self.assertTrue(os.access(installed / 'Contents/MacOS/Juicy16', os.X_OK))
            else:
                self.assertFalse(installed.exists(), str(installed))

    def test_default_installs_all_three_formats_and_verifies_before_copying(self):
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_selection({'au', 'vst3', 'app'})
        calls = self.commands()
        first_copy = next(i for i, call in enumerate(calls) if call[0] == 'ditto')
        sources_verified = [call[-1] for call in calls[:first_copy] if call[0] == 'codesign']
        self.assertEqual(set(sources_verified), {str(self.package / source) for source, _ in FORMATS.values()})
        quarantines = [call for call in calls if call[0] == 'xattr']
        self.assertEqual(len(quarantines), 3)
        self.assertTrue(all(call[1:3] == ['-dr', 'com.apple.quarantine'] for call in quarantines))

    def test_each_selection_only_installs_requested_formats(self):
        for choice, selected in (('2', {'au', 'vst3'}), ('3', {'app'}), ('4', {'au'}), ('5', {'vst3'})):
            with self.subTest(choice=choice):
                result = self.run_installer(choice)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assert_selection(selected)
                for _, target in FORMATS.values():
                    installed = self.home / target
                    if installed.exists():
                        shutil.rmtree(installed)

    def test_replacing_app_preserves_complete_previous_bundle_in_backup(self):
        old_app = self.home / FORMATS['app'][1]
        self.bundle(old_app, b'previous-app')
        (old_app / 'previous-extra.txt').write_bytes(b'keep me')
        result = self.run_installer('3')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        backups = list((self.home / 'Library/Audio/Plug-Ins').glob('.juicy16-backup-*'))
        self.assertEqual(len(backups), 1)
        backup = backups[0] / 'Juicy16.app'
        self.assertEqual((backup / 'Contents/MacOS/Juicy16').read_bytes(), b'previous-app')
        self.assertEqual((backup / 'previous-extra.txt').read_bytes(), b'keep me')
        self.assertEqual((old_app / 'Contents/MacOS/Juicy16').read_bytes(), b'app-new')
        self.assertFalse((old_app / 'previous-extra.txt').exists())

    def test_tampered_or_missing_manifest_installs_nothing(self):
        app = self.package / FORMATS['app'][0] / 'Contents/MacOS/Juicy16'
        app.write_bytes(b'tampered')
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assert_selection(set())
        self.assertFalse(any(call[0] == 'ditto' for call in self.commands()))
        self.manifest()
        (self.package / 'SHA256SUMS').unlink()
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assert_selection(set())

    def test_missing_selected_bundle_fails_before_any_format_is_installed(self):
        shutil.rmtree(self.package / FORMATS['app'][0])
        self.manifest()
        result = self.run_installer('1')
        self.assertNotEqual(result.returncode, 0)
        self.assert_selection(set())
        self.assertFalse(any(call[0] == 'ditto' for call in self.commands()))

    def test_wrong_bundle_architecture_and_source_signature_are_preflight_failures(self):
        for overrides in ({'JUICY16_TEST_BUNDLE_ARCH': 'arm64 x86_64'}, {'JUICY16_TEST_FAIL_SOURCE': '1'}):
            with self.subTest(overrides=overrides):
                result = self.run_installer('1', **overrides)
                self.assertNotEqual(result.returncode, 0)
                self.assert_selection(set())
                self.assertFalse(any(call[0] == 'ditto' for call in self.commands()))

    def test_copy_or_staged_signature_failure_retains_existing_app(self):
        old_app = self.home / FORMATS['app'][1]
        self.bundle(old_app, b'previous-app')
        for overrides in ({'JUICY16_TEST_FAIL_COPY': '1'}, {'JUICY16_TEST_FAIL_STAGE': '1'}):
            with self.subTest(overrides=overrides):
                result = self.run_installer('3', **overrides)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual((old_app / 'Contents/MacOS/Juicy16').read_bytes(), b'previous-app')
                self.assertFalse(list(self.home.rglob('.juicy16-backup-*')))

    def test_final_location_signature_failure_restores_previous_app(self):
        old_app = self.home / FORMATS['app'][1]
        self.bundle(old_app, b'previous-app')
        (old_app / 'previous-extra.txt').write_bytes(b'keep me')
        result = self.run_installer('3', JUICY16_TEST_FAIL_FINAL='1')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((old_app / 'Contents/MacOS/Juicy16').read_bytes(), b'previous-app')
        self.assertEqual((old_app / 'previous-extra.txt').read_bytes(), b'keep me')
        backups = list((self.home / 'Library/Audio/Plug-Ins').glob('.juicy16-backup-*'))
        self.assertEqual(len(backups), 1)
        self.assertEqual((backups[0] / 'Juicy16.app/Contents/MacOS/Juicy16').read_bytes(), b'previous-app')

    def test_final_location_signature_failure_removes_new_app_without_previous_version(self):
        result = self.run_installer('3', JUICY16_TEST_FAIL_FINAL='1')
        self.assertNotEqual(result.returncode, 0)
        self.assert_selection(set())



if __name__ == '__main__':
    unittest.main()
