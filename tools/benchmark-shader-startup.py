"""Compare complete Vulkan preloads using an isolated cache snapshot on Windows.

Copies shader records and hard-links driver binary payloads. Supported binaries
write those payloads by atomic replacement, so the original cache is unchanged.
No saves/settings are copied. Baseline processes are stopped after the preload
marker; candidate processes drain cache writes and exit before gameplay.
Reboot-cold measurements require preparing once, rebooting, then --only MODE.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import queue
import re
import shutil
import statistics
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'launcher'))
import kzsf_local as ui

MARKER = re.compile(r'Pipeline warm-up: (\d+) of (\d+) pipelines in ([\d.]+) s')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, default=ROOT/'out/gpu/kzsf-probe.exe')
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--only', choices=('baseline', 'loose', 'packed'))
    parser.add_argument('--reboot-cold', action='store_true')
    parser.add_argument('--runs', type=int, default=3)
    args = parser.parse_args()
    work = args.work.resolve()
    if not work.is_relative_to((ROOT/'out').resolve()):
        parser.error('Work must be an isolated directory under out/.')
    if args.reboot_cold and not args.only:
        parser.error('A reboot-cold invocation must select --only MODE.')
    if not args.reboot_cold and args.runs < 3:
        parser.error('Repeat measurements require at least three runs.')
    baseline, candidate = args.baseline.resolve(), args.candidate.resolve()
    if not baseline.is_file() or not candidate.is_file():
        parser.error('Both executables must exist.')
    cache = work/'user/cache/CUSA00190'
    if not (work/'snapshot.json').is_file():
        if work.exists() or args.source is None:
            parser.error('First use needs --source and a new work directory.')
        source = args.source.resolve()
        if source.is_relative_to(work) or work.is_relative_to(source):
            parser.error('Source and work must be separate.')
        cache.mkdir(parents=True)
        if shutil.disk_usage(work).free < 1024**3:
            parser.error('At least 1 GiB free space is needed for shader copies and indexes.')
        tasks = [(p, cache/p.name, False) for p in source.iterdir() if p.is_file() and
                 (p.suffix in ('.key', '.meta', '.spv') or p.name == 'profile.bin')]
        driver = source/'pipeline-binaries'
        if driver.is_dir():
            for folder, dirs, files in os.walk(driver):
                relative = Path(folder).relative_to(source)
                (cache/relative).mkdir(parents=True, exist_ok=True)
                tasks.extend((Path(folder)/name, cache/relative/name, True)
                             for name in files if not name.endswith('.tmp'))
        def copy(task):
            src, dst, link = task
            if link:
                os.link(src, dst)  # no copy fallback: fail if the volume differs
            else:
                shutil.copy2(src, dst)
        print(f'Preparing {len(tasks)} isolated cache entries.', flush=True)
        with ThreadPoolExecutor(max_workers=8) as pool:
            for offset in range(0, len(tasks), 512):
                list(pool.map(copy, tasks[offset:offset+512]))
                if offset % 32768 == 0:
                    print('Snapshot entries:', min(offset+512, len(tasks)), flush=True)
        (work/'snapshot.json').write_text(json.dumps(dict(source=str(source), entries=len(tasks))))
    if args.prepare_only:
        print('Snapshot ready. Run repeat benchmarks, or reboot before a cold measurement.')
        return
    running = subprocess.check_output(['tasklist', '/FO', 'CSV', '/NH'], text=True)
    if re.search(r'"kzsf-probe[^\"]*\.exe"', running, re.I):
        parser.error('Close the game before benchmarking; no running game is stopped automatically.')
    values = dict(ui.DEFAULTS)
    values['game_dir'] = ui.load_preferences()['game_dir']
    results = []

    def run(mode, index):
        folder = work/f'{mode}-{time.time_ns()}'
        folder.mkdir()
        spec = ui.launch_spec(values, folder)
        env = spec['env']
        env.update(KZSF_USER_DIR=str(work/'user'), KZSF_GPU_USER_DIR=str(work/'user'),
                   KZSF_CONFIG=str(work/'settings.ini'), KZSF_FRAME_STATS='0',
                   KZSF_STALL_SECONDS='0',
                   KZSF_PARALLEL_WARMUP='0' if mode == 'baseline' else '1',
                   KZSF_PACKED_SHADER_CACHE='1' if mode == 'packed' else '0',
                   KZSF_CACHE_KEEP_LOOSE='1', KZSF_CACHE_STARTUP_BENCHMARK='1')
        command = list(spec['command'])
        command[0] = str(baseline if mode == 'baseline' else candidate)
        started = time.monotonic()
        process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, errors='replace',
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        lines = queue.Queue()
        def reader():
            with (folder/'stdout.log').open('w', encoding='utf-8') as log:
                for line in process.stdout:
                    log.write(line)
                    log.flush()
                    lines.put((time.monotonic(), line))
            lines.put((time.monotonic(), None))
        thread = threading.Thread(target=reader, daemon=True)
        thread.start()
        summary = metrics = None
        try:
            while time.monotonic()-started < 900:
                try:
                    timestamp, line = lines.get(timeout=1)
                except queue.Empty:
                    continue
                if line is None:
                    break
                match = MARKER.search(line)
                if match:
                    summary = dict(mode=mode, run=index, loaded=int(match[1]), total=int(match[2]),
                                   reported_seconds=float(match[3]), startup_seconds=timestamp-started,
                                   reboot_cold=args.reboot_cold, log=str(folder/'stdout.log'))
                    if mode == 'baseline':
                        break
                if line.startswith('Shader cache metrics:'):
                    metrics = dict(re.findall(r'(\w+)=([^\s]+)', line))
            if summary is None:
                raise RuntimeError(f'No completed preload marker; see {folder}')
            if mode != 'baseline':
                process.wait(timeout=60)
                if process.returncode or metrics is None:
                    raise RuntimeError(f'Candidate failed startup/exit check; see {folder}')
            summary['metrics'] = metrics
            print(summary, flush=True)
            return summary
        finally:
            if process.poll() is None:
                process.terminate()  # only the private benchmark child
                process.wait(timeout=30)
            thread.join(timeout=10)

    modes = (args.only,) if args.only else ('baseline', 'loose', 'packed')
    if 'packed' in modes and not (cache/'packed-v1').is_dir():
        migration = run('packed', 'migration')
        (work/'migration.json').write_text(json.dumps(migration, indent=2))
        if args.reboot_cold:
            raise RuntimeError('Migration complete; reboot again before claiming a cold packed startup.')
    for index in range(1 if args.reboot_cold else args.runs):
        for mode in modes[index % len(modes):] + modes[:index % len(modes)]:
            results.append(run(mode, index+1))
    medians = {m: statistics.median(r['startup_seconds'] for r in results if r['mode'] == m) for m in modes}
    comparable = len({(r['loaded'], r['total']) for r in results}) == 1
    hashes = {r['metrics']['pipeline_set_hash'] for r in results if r['metrics']}
    report = dict(results=results, median_startup_seconds=medians, counts_match=comparable,
                  candidate_sets_match=len(hashes) == 1, reboot_cold=args.reboot_cold)
    if len(modes) == 3:
        report['packed_improvement_percent'] = 100*(1-medians['packed']/medians['baseline'])
    path = work/f'results-{time.time_ns()}.json'
    path.write_text(json.dumps(report, indent=2)+'\n')
    print('Results:', path, flush=True)


if __name__ == '__main__':
    main()
