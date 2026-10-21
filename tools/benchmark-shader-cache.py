"""Benchmark shader record IO on an isolated copy, never the original cache.

This is not a full-startup benchmark: GPU driver pipeline binaries are excluded.
It cannot enable packed storage by default or satisfy the 20% startup gate.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import shutil
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--exe', type=Path, default=ROOT/'out/gpu/benchmark-shader-cache.exe')
    parser.add_argument('--runs', type=int, default=3)
    args = parser.parse_args()
    source, work = args.source.resolve(), args.work.resolve()
    if args.runs < 3 or not work.is_relative_to((ROOT/'out').resolve()) or work.is_relative_to(source) or source.is_relative_to(work):
        parser.error('Use at least 3 runs and a separate, new work directory under out/.')
    if work.exists():
        parser.error('Work directory must be new; previous results are never overwritten.')
    work.mkdir(parents=True)
    cache = work/'cache'
    cache.mkdir()
    records = [p for p in source.iterdir() if p.is_file() and
               (p.suffix in ('.key', '.meta', '.spv') or p.name == 'profile.bin')]
    if not records:
        parser.error('No shader records at source; pass the CUSA00190 cache directory.')
    total = sum(p.stat().st_size for p in records)
    if shutil.disk_usage(work).free < total*2 + 512*1024*1024:
        parser.error('Insufficient space for a copy, packed data and safety margin.')
    print(f'Copying {len(records)} shader/profile files ({total} bytes); driver binaries excluded.', flush=True)
    with ThreadPoolExecutor(max_workers=8) as pool:
        for i in range(0, len(records), 256):
            list(pool.map(lambda p: shutil.copy2(p, cache/p.name), records[i:i+256]))
    (cache/'.benchmark-copy').write_text('Isolated storage benchmark; no saves or game data.\n')

    def run(mode):
        completed = subprocess.run([str(args.exe.resolve()), mode, str(cache)],
                                   capture_output=True, text=True, check=True)
        result = json.loads(completed.stdout.splitlines()[-1])
        print(result, flush=True)
        return result

    migration = run('migrate')  # measured separately, never counted as a repeat launch
    results = []
    for i in range(args.runs):
        # Rotate order to reduce filesystem-cache/order bias.
        modes = ('original', 'loose', 'packed')
        for mode in modes[i % 3:] + modes[:i % 3]:
            results.append(run(mode))
    assert len({(r['records'], r['bytes'], r['digest']) for r in [migration]+results}) == 1
    medians = {m: statistics.median(r['ms'] for r in results if r['mode'] == m)
               for m in ('original', 'loose', 'packed')}
    report = dict(scope='storage-only; warm OS cache; no Vulkan pipelines or driver binaries',
                  source=str(source), runs=args.runs, migration=migration, results=results,
                  median_ms=medians, packed_speedup_percent=100*(1-medians['packed']/medians['original']),
                  full_startup_acceptance_verified=False)
    (work/'results.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report['median_ms'], indent=2))
    print('Full-startup and reboot-cold validation still required. Results:', work/'results.json')


if __name__ == '__main__':
    main()
