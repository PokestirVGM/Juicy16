"""Fast, optional tests for benchmark parsing and paired comparison logic."""
import csv
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


SPEC = importlib.util.spec_from_file_location(
    'benchmark_compare', Path(__file__).resolve().parents[1] / 'tools/benchmark_compare.py')
BENCHMARK = importlib.util.module_from_spec(SPEC)
previous_bytecode = sys.dont_write_bytecode
sys.dont_write_bytecode = True
try:
    SPEC.loader.exec_module(BENCHMARK)
finally:
    sys.dont_write_bytecode = previous_bytecode


def measurement(render=100.0):
    row = dict(zip(BENCHMARK.COLUMNS, (
        'single', 48000, 64, 0, 375, 0, 1, 1, 0.5, render,
        0.25, 0.5, 1.0, 0.0, 2.0, 0.0, 1.0, 0.5, 1)))
    return row


def output(row=None):
    row = measurement() if row is None else row
    stream = io.StringIO()
    writer = csv.writer(stream)
    writer.writerow(('BENCH_SCHEMA', 1))
    writer.writerow(('BENCH_HEADER', *BENCHMARK.COLUMNS))
    writer.writerow(('BENCH', *(row[column] for column in BENCHMARK.COLUMNS)))
    return stream.getvalue()


class BenchmarkCompareScriptTests(unittest.TestCase):
    def test_parser_accepts_schema_with_framework_logging(self):
        rows = BENCHMARK.parse_measurements('FluidSynth diagnostic\n' + output())
        self.assertEqual(rows[('single', 48000, 64)]['render_us'], 100.0)

    def test_parser_fails_closed_on_empty_malformed_duplicate_and_nonfinite(self):
        for bad in ('', 'BENCH_SCHEMA,2\n' + output(), output() + output(),
                    output().replace('BENCH_HEADER', 'UNKNOWN_HEADER'),
                    output(measurement(float('nan'))), output(measurement(0.0))):
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    BENCHMARK.parse_measurements(bad)

    def test_changed_workload_and_audio_diagnostics_are_rejected(self):
        before = BENCHMARK.parse_measurements(output())
        for field, value in (('voices_start', 2), ('blocks', 100), ('audio_energy', 3.0)):
            with self.subTest(field=field):
                changed = measurement()
                changed[field] = value
                with self.assertRaises(ValueError):
                    BENCHMARK.compare_workload(before, BENCHMARK.parse_measurements(output(changed)))

    def test_median_summary_uses_pairs_and_resists_outlier(self):
        rounds = [
            {'baseline': BENCHMARK.parse_measurements(output(measurement(before))),
             'candidate': BENCHMARK.parse_measurements(output(measurement(after)))}
            for before, after in ((100.0, 50.0), (1000.0, 500.0), (100.0, 50.0))]
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'summary.csv'
            BENCHMARK.write_summary(rounds, path)
            with path.open(encoding='utf-8') as stream:
                row = next(csv.DictReader(stream))
            self.assertEqual(float(row['baseline_render_median_us']), 100.0)
            self.assertEqual(float(row['candidate_render_median_us']), 50.0)
            self.assertEqual(float(row['paired_render_change_percent']), -50.0)
            self.assertEqual(float(row['baseline_render_mad_us']), 0.0)

    def test_runner_alternates_binaries_and_saves_every_raw_run(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for filename in ('before', 'after', 'bank.dls'):
                (root / filename).write_bytes(filename.encode())
            destination = root / 'result'
            arguments = ['benchmark_compare.py', '--baseline', str(root / 'before'),
                         '--candidate', str(root / 'after'), '--bank', str(root / 'bank.dls'),
                         '--output', str(destination), '--rounds', '3']
            launched = []
            def fake_run(command, **kwargs):
                launched.append(Path(command[0]).name)
                self.assertIn('--repeat=1', command)
                self.assertTrue(kwargs['capture_output'])
                return subprocess.CompletedProcess(command, 0, output(measurement(
                    100.0 if launched[-1] == 'before' else 50.0)), 'diagnostic\n')
            with mock.patch.object(sys, 'argv', arguments), mock.patch.object(
                    BENCHMARK.subprocess, 'run', side_effect=fake_run), mock.patch('sys.stdout', io.StringIO()), \
                    mock.patch.object(BENCHMARK.platform, 'platform', return_value='test-platform'), \
                    mock.patch.object(BENCHMARK.platform, 'machine', return_value='test-machine'):
                BENCHMARK.main()
            self.assertEqual(launched, ['before', 'after', 'after', 'before', 'before', 'after'])
            self.assertEqual(len(list(destination.glob('*.stdout.log'))), 6)
            self.assertEqual(len(list(destination.glob('*.stderr.log'))), 6)
            self.assertTrue((destination / 'metadata.json').is_file())
            self.assertTrue((destination / 'summary.csv').is_file())


if __name__ == '__main__':
    unittest.main()
