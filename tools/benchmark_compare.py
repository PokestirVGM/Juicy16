#!/usr/bin/env python3
"""Compare matching JuicySFPerfProbe --benchmark builds in alternating order.

Example:
  python3 tools/benchmark_compare.py --baseline /tmp/before/JuicySFPerfProbe \
      --candidate /tmp/after/JuicySFPerfProbe --bank /path/to/bank.dls \
      --output /tmp/juicy16-comparison --rounds 5 --seconds 0.5

Both binaries must contain the same benchmark harness. This is optional local
measurement tooling; it performs no builds, installation, or plugin scanning.
"""
import argparse
import csv
import hashlib
import io
import json
import math
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import time


COLUMNS = ('scenario', 'rate', 'block', 'repeat', 'blocks', 'events_per_block',
           'voices_start', 'voices_end', 'audio_seconds', 'render_us',
           'block_median_us', 'block_p95_us', 'block_max_us', 'midi_generate_us',
           'midi_copy_us', 'parameter_update_us', 'audio_energy', 'peak', 'finite')
INT_COLUMNS = {'rate', 'block', 'repeat', 'blocks', 'events_per_block',
               'voices_start', 'voices_end', 'finite'}
WORKLOAD_COLUMNS = ('rate', 'block', 'blocks', 'events_per_block', 'voices_start', 'voices_end')


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def parse_measurements(output):
    """Ignore human/framework logging; require complete schema and unique rows."""
    schema = False
    header = False
    measurements = {}
    for fields in csv.reader(io.StringIO(output)):
        if not fields:
            continue
        if fields[0] == 'BENCH_SCHEMA':
            if fields != ['BENCH_SCHEMA', '1'] or schema:
                raise ValueError('Missing/duplicate/unsupported benchmark schema')
            schema = True
        elif fields[0] == 'BENCH_HEADER':
            if tuple(fields[1:]) != COLUMNS or header:
                raise ValueError('Unexpected/duplicate benchmark column header')
            header = True
        elif fields[0] == 'BENCH':
            if not schema or not header or len(fields) != len(COLUMNS) + 1:
                raise ValueError('Incomplete benchmark row')
            row = dict(zip(COLUMNS, fields[1:]))
            for column in COLUMNS[1:]:
                row[column] = int(row[column]) if column in INT_COLUMNS else float(row[column])
                if not math.isfinite(row[column]):
                    raise ValueError(f'Non-finite benchmark measurement: {column}')
            if row['finite'] != 1 or row['blocks'] <= 0 or row['audio_seconds'] <= 0 or row['render_us'] <= 0:
                raise ValueError('Invalid/inactive benchmark measurement')
            if row['repeat'] != 0:
                raise ValueError('Comparison runner expects --repeat=1')
            key = (row['scenario'], row['rate'], row['block'])
            if key in measurements:
                raise ValueError(f'Duplicate scenario: {key}')
            measurements[key] = row
    if not schema or not header or not measurements:
        raise ValueError('No complete benchmark measurements')
    return measurements


def compare_workload(before, after):
    if before.keys() != after.keys():
        raise ValueError('Baseline and candidate measured different scenario sets')
    for key, baseline in before.items():
        candidate = after[key]
        for column in WORKLOAD_COLUMNS:
            if baseline[column] != candidate[column]:
                raise ValueError(f'Changed benchmark workload {key}: {column}')
        if not math.isclose(baseline['audio_seconds'], candidate['audio_seconds'], rel_tol=0, abs_tol=1e-9):
            raise ValueError(f'Changed audio duration: {key}')
        # These catch gross workload changes; waveform equivalence remains the
        # job of the registered rendering tests, not aggregate energy/peak.
        for column in ('audio_energy', 'peak'):
            if not math.isclose(baseline[column], candidate[column], rel_tol=1e-6, abs_tol=1e-9):
                raise ValueError(f'Changed benchmark audio diagnostic {key}: {column}')


def median_and_mad(values):
    median = statistics.median(values)
    return median, statistics.median(abs(value - median) for value in values)


def write_summary(rounds, destination):
    columns = ('scenario', 'rate', 'block', 'rounds', 'baseline_render_median_us',
               'candidate_render_median_us', 'baseline_render_mad_us', 'candidate_render_mad_us',
               'baseline_realtime_percent', 'candidate_realtime_percent', 'paired_render_change_percent',
               'baseline_block_p95_median_us', 'candidate_block_p95_median_us',
               'baseline_parameter_update_median_us', 'candidate_parameter_update_median_us',
               'baseline_render_plus_parameter_median_us', 'candidate_render_plus_parameter_median_us')
    with destination.open('w', newline='', encoding='utf-8') as output:
        writer = csv.writer(output)
        writer.writerow(columns)
        for key in sorted(rounds[0]['baseline']):
            before = [pair['baseline'][key] for pair in rounds]
            after = [pair['candidate'][key] for pair in rounds]
            baseline, baseline_mad = median_and_mad([row['render_us'] for row in before])
            candidate, candidate_mad = median_and_mad([row['render_us'] for row in after])
            duration_us = before[0]['audio_seconds'] * 1_000_000
            paired_change = statistics.median(100 * (a['render_us'] / b['render_us'] - 1)
                                               for b, a in zip(before, after))
            values = (*key, len(rounds), baseline, candidate, baseline_mad, candidate_mad,
                      100 * baseline / duration_us, 100 * candidate / duration_us, paired_change,
                      statistics.median(row['block_p95_us'] for row in before),
                      statistics.median(row['block_p95_us'] for row in after),
                      statistics.median(row['parameter_update_us'] for row in before),
                      statistics.median(row['parameter_update_us'] for row in after),
                      statistics.median(row['render_us'] + row['parameter_update_us'] for row in before),
                      statistics.median(row['render_us'] + row['parameter_update_us'] for row in after))
            writer.writerow([f'{value:.6f}' if isinstance(value, float) else value for value in values])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--bank', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=5)
    parser.add_argument('--seconds', type=float, default=0.5)
    parser.add_argument('--scenario')
    parser.add_argument('--rate', type=int, choices=(44100, 48000, 96000, 192000))
    parser.add_argument('--block-size', type=int)
    parser.add_argument('--interpolation', choices=('linear', 'seventh', 'none'), default='linear')
    args = parser.parse_args()
    if args.rounds < 3 or args.rounds > 100 or not math.isfinite(args.seconds) or not 0.1 <= args.seconds <= 30:
        parser.error('Use 3-100 rounds and 0.1-30 seconds of audio per scenario')
    if args.block_size is not None and not 64 <= args.block_size <= 4096:
        parser.error('Block size must be 64-4096')
    for path in (args.baseline, args.candidate, args.bank):
        if not path.is_file():
            parser.error(f'Input file not found: {path}')
    args.output.mkdir(parents=True, exist_ok=False)
    binaries = {'baseline': args.baseline.resolve(), 'candidate': args.candidate.resolve()}
    benchmark_args = [str(args.bank.resolve()), '--benchmark', '--repeat=1', f'--seconds={args.seconds}']
    for option in ('scenario', 'rate', 'block_size', 'interpolation'):
        value = getattr(args, option)
        if value is not None:
            benchmark_args.append(f'--{option.replace("_", "-")}={value}')
    metadata = {'schema': 1, 'platform': platform.platform(), 'machine': platform.machine(),
                'started_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
                'bank': str(args.bank.resolve()), 'bank_sha256': file_sha256(args.bank),
                'binaries': {label: {'path': str(path), 'sha256': file_sha256(path)} for label, path in binaries.items()},
                'rounds': args.rounds, 'benchmark_args': benchmark_args,
                'order': 'baseline/candidate on even rounds, candidate/baseline on odd rounds',
                'diagnostics': 'Energy/peak checks are workload diagnostics, not waveform equivalence.'}
    (args.output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    rounds = []
    for round_number in range(args.rounds):
        labels = ('baseline', 'candidate') if round_number % 2 == 0 else ('candidate', 'baseline')
        pair = {}
        for label in labels:
            print(f'Round {round_number + 1}/{args.rounds}: {label}', flush=True)
            result = subprocess.run([str(binaries[label]), *benchmark_args], capture_output=True, text=True)
            stem = args.output / f'round-{round_number:02d}-{label}'
            stem.with_suffix('.stdout.log').write_text(result.stdout, encoding='utf-8')
            stem.with_suffix('.stderr.log').write_text(result.stderr, encoding='utf-8')
            if result.returncode != 0:
                raise RuntimeError(f'{label} benchmark failed (exit {result.returncode}); see {stem}.stderr.log')
            pair[label] = parse_measurements(result.stdout)
        compare_workload(pair['baseline'], pair['candidate'])
        if rounds:
            compare_workload(rounds[0]['baseline'], pair['baseline'])
            compare_workload(rounds[0]['candidate'], pair['candidate'])
        rounds.append(pair)
    write_summary(rounds, args.output / 'summary.csv')
    print(f'Compared {len(rounds[0]["baseline"])} scenarios over {args.rounds} alternating rounds: {args.output / "summary.csv"}')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
