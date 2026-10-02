"""Exercise gate orchestration and failure handling without building plugins."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


STUB = r'''import json, os, pathlib, sys
name = pathlib.Path(sys.argv[0]).name
with open(os.environ['MOCK_CALLS'], 'a', encoding='utf-8') as log:
    log.write(json.dumps([name, *sys.argv[1:]]) + '\n')
if name == 'cmake' and '--build' in sys.argv:
    sys.exit(int(os.environ.get('MOCK_BUILD_STATUS', '0')))
if name == 'ctest':
    sys.exit(int(os.environ.get('MOCK_TEST_STATUS', '0')))
if name == 'leaks':
    scenario = os.environ.get('MOCK_LEAK_SCENARIO', 'pass')
    harness = pathlib.Path(sys.argv[sys.argv.index('--') + 1]).name
    completion = {
        'JuicySFFontQA': '== summary: 1 ok (0 auto-repaired), 0 failed, 0 unit-test failures ==',
        'JuicySFEngineMidiTests': '== engine_midi_tests: 0 failures ==',
        'JuicySFVST3Smoke': '== vst3_smoke: 0 failures ==',
        'JuicySFAUSmoke': '== au_smoke: 0 failures ==',
    }[harness]
    if scenario != 'no_completion':
        print(completion.replace('0 failures', '1 failures') if scenario == 'failed_completion' else completion)
    if scenario == 'indented_failure':
        print('    FAIL an engine assertion')
    if scenario != 'no_leak_summary':
        print('Process 1: ' + ('10 leaks for 0 total leaked bytes' if scenario == 'nonzero_count' else '0 leaks for 0 total leaked bytes.'))
    sys.exit(255 if scenario == 'leaks_error' else 0)
'''


class CiGateScriptTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='juicy16-gates-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'tools').mkdir()
        script = Path(__file__).resolve().parents[1] / 'tools/ci_gates.sh'
        shutil.copy2(script, self.root / 'tools/ci_gates.sh')
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        for name in ('cmake', 'ctest', 'leaks'):
            path = self.bin / name
            path.write_text(f'#!{sys.executable}\n' + STUB, encoding='utf-8')
            path.chmod(0o755)
        deps = self.root / 'deps'
        (deps / 'lib/pkgconfig').mkdir(parents=True)
        (deps / 'lib/pkgconfig/fluidsynth.pc').touch()
        (deps / 'include/fluidsynth').mkdir(parents=True)
        (deps / 'include/fluidsynth/synth.h').write_text(
            '#define FLUIDSYNTH_JUICY16_VIBRATO_SCALE 1\n'
            '#define FLUIDSYNTH_JUICY16_DLS_FULL_PAN 1\n', encoding='utf-8')
        # A stale executable must not cause configure/build/CTest to be skipped.
        stale = self.root / 'build-ci-debug/JuicySFEngineMidiTests'
        stale.parent.mkdir()
        stale.touch()
        stale.chmod(0o755)
        self.calls_path = self.root / 'calls.jsonl'
        self.env = {**os.environ, 'PATH': str(self.bin) + os.pathsep + os.environ['PATH'],
                    'JUICY16_DEPS_PREFIX': str(deps), 'MOCK_CALLS': str(self.calls_path)}

    def gate(self, gate='leaks', **env):
        return subprocess.run(['bash', str(self.root / 'tools/ci_gates.sh'), gate],
                              env={**self.env, **env}, text=True, capture_output=True)

    def calls(self):
        return [json.loads(line) for line in self.calls_path.read_text(encoding='utf-8').splitlines()]

    def test_leaks_refreshes_stale_debug_and_checks_all_harnesses(self):
        result = self.gate()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        calls = self.calls()
        self.assertEqual([call[0] for call in calls[:3]], ['cmake', 'cmake', 'ctest'])
        self.assertIn('--build', calls[1])
        self.assertEqual(len([call for call in calls if call[0] == 'leaks']), 4)

    def test_leaks_fails_closed_on_bad_tool_or_harness_evidence(self):
        for scenario in ('leaks_error', 'no_completion', 'failed_completion',
                         'indented_failure', 'no_leak_summary', 'nonzero_count'):
            with self.subTest(scenario=scenario):
                result = self.gate(MOCK_LEAK_SCENARIO=scenario)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertNotIn("Gate 'leaks' passed", result.stdout)

    def test_failed_debug_preflight_stops_before_leak_inspection(self):
        for variable in ('MOCK_BUILD_STATUS', 'MOCK_TEST_STATUS'):
            with self.subTest(variable=variable):
                self.calls_path.unlink(missing_ok=True)
                result = self.gate(**{variable: '7'})
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(any(call[0] == 'leaks' for call in self.calls()))

    def test_all_reuses_debug_only_within_same_invocation(self):
        result = self.gate('all')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        debug_builds = [call for call in self.calls() if '--build' in call and 'build-ci-debug' in call]
        self.assertEqual(len(debug_builds), 1)
        for call in self.calls():
            if call[0] == 'cmake' and '-S' in call:
                self.assertIn('-DBUILD_TESTING=ON', call)
            elif call[0] == 'ctest':
                self.assertIn('--no-tests=error', call)

    def test_asan_covers_both_playback_harnesses_and_fails_on_empty_selection(self):
        result = self.gate('asan')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        build = next(call for call in self.calls() if '--build' in call)
        for target in ('JuicySFFontQA', 'JuicySFEngineMidiTests',
                       'JuicySFPlaybackReliabilityTests', 'JuicySFRenderEquivalenceTests'):
            self.assertIn(target, build)
        ctest = next(call for call in self.calls() if call[0] == 'ctest')
        self.assertIn('--no-tests=error', ctest)
        regex = ctest[ctest.index('-R') + 1]
        self.assertIn('playback_reliability', regex)
        self.assertIn('render_equivalence', regex)
        self.assertTrue(regex.startswith('^(') and regex.endswith(')$'))


if __name__ == '__main__':
    unittest.main()
