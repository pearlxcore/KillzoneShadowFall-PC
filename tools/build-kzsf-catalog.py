"""Bind a discovery inventory and pipeline index to an exact local game build.

Exports metadata only. This is not an offline-compilable pipeline recipe.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from prepare import read_sfo


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('--inventory', required=True, type=Path)
    parser.add_argument('--cache', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    info = read_sfo(args.game / 'sce_sys/param.sfo')
    if (info.get('TITLE_ID'), info.get('APP_VER')) != ('CUSA00190', '01.81'):
        raise ValueError('Catalog requires CUSA00190 / 01.81')
    inventory = json.loads(args.inventory.read_text(encoding='utf-8'))
    if inventory['failures']:
        raise ValueError('Resolve discovery failures before exporting')
    files = {args.game / x['file'] for x in inventory['shaders']}
    files.update((args.game / 'localcachepink/lumps').glob('*.core*'))
    identity = ['eboot.bin', 'all_programs.prx', 'sce_sys/param.sfo',
                'sce_module/libc.prx', 'sce_module/libSceFios2.prx']
    files.update(args.game / name for name in identity)
    records = []
    for index, path in enumerate(sorted(files)):
        records.append(dict(file=path.relative_to(args.game).as_posix(),
                            bytes=path.stat().st_size, sha256=digest(path)))
        if index % 100 == 0:
            print(f'Fingerprinting assets: {index + 1}/{len(files)}', flush=True)
    fingerprint = hashlib.sha256(json.dumps(records, sort_keys=True).encode()).hexdigest()
    pipelines = [dict(file=p.name, bytes=p.stat().st_size, sha256=digest(p))
                 for p in sorted(args.cache.glob('*.key'))]
    programs = sorted({p.stem.split('_')[0].removeprefix('0x').zfill(16)
                       for p in args.cache.glob('*.spv')})
    discovered = {s['hash'] for s in inventory['shaders']}
    result = dict(format=1, title_id='CUSA00190', app_version='01.81',
                  catalog_id=fingerprint, status='inventory; offline compilation unavailable',
                  identity_files=identity, files=records, shaders=inventory['shaders'],
                  recorded_pipelines=pipelines, recorded_programs=programs,
                  undiscovered_programs=sorted(set(programs) - discovered))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f'Catalog {fingerprint}: {len(pipelines)} pipelines, {len(programs)} programs')


if __name__ == '__main__':
    main()
