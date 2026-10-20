"""Read-only RTTIBin model-resource inventory across every local lump bundle.

Decodes bounded headers, never object payloads or model geometry. Patch deltas
are listed separately and are not merged. No game process access is required.
"""
import argparse
from collections import Counter, defaultdict
from datetime import datetime, timezone
import html
import importlib.util
import json
from pathlib import Path
import struct
import sys
from kzsf_core_index import MAX_HEADER, parse_index

ROOT = Path(__file__).resolve().parents[1]


def model_header(path, decoder, blocks):
    with path.open('rb') as stream:
        header = stream.read(32)
    decoded_size = struct.unpack_from('<Q', header, 8)[0] if len(header) == 32 and header[:4] == bytes.fromhex('83c110cb') else path.stat().st_size
    prefix = bytearray()
    last_error = None
    for offset, block in blocks(path, decoder):
        if offset != len(prefix):
            raise ValueError('Non-contiguous header decode')
        prefix.extend(block[:MAX_HEADER-len(prefix)])
        try:
            return parse_index(bytes(prefix), decoded_size)
        except ValueError as error:
            if str(error) != 'Truncated or oversized RTTIBin header':
                raise
            last_error = error
        if len(prefix) >= MAX_HEADER:
            break
    raise last_error or ValueError('Empty asset')


def browser(report):
    e = html.escape
    rows = []
    for bundle in report['bundles']:
        summary = ', '.join(f'{name}: {count}' for name, count in bundle['model_type_counts'].items())
        rows.append('<tr><td><code>'+e(bundle['file'])+'</code></td><td>'+e(summary)+'</td><td><a href="'+e(bundle['browser_page'],quote=True)+'">'+str(len(bundle['objects']))+' resource identities</a></td></tr>')
    stats = report['summary']
    return '''<!doctype html><html lang="en"><meta charset="utf-8"><title>Killzone model collection</title>
<meta name="viewport" content="width=device-width,initial-scale=1"><style>
body{font:15px system-ui;background:#111820;color:#e1e9ef;margin:32px}p{max-width:1100px;line-height:1.6;color:#b8c8d5}a{color:#9acbff}input{padding:12px;width:70%;background:#233240;color:white;border:1px solid #567083}table{border-collapse:collapse;width:100%;margin-top:20px}td,th{padding:10px;border-bottom:1px solid #30404e;text-align:left;vertical-align:top}th{background:#253645;position:sticky;top:0}code{font-size:11px;overflow-wrap:anywhere}tr[hidden]{display:none}ul{max-height:400px;overflow:auto;padding-left:20px}</style>
<h1>Killzone model collection</h1><p>All local lump base-file headers scanned: '''+str(stats['parsed_base_files'])+''' parsed, '''+str(stats['patch_files'])+''' patch deltas listed separately, '''+str(stats['failures'])+''' failures. '''+str(stats['bundles_with_model_resources'])+''' bundles contain typed model resources; '''+str(stats['unique_resource_identities'])+''' distinct type/UUID identities were found.</p>
<p><a href="character-catalog.html">Character bundle catalog and current selector support</a></p>
<p>This inventory includes characters, scenery and other model-bearing bundles. It lists serialized model/mesh/skeleton identities, not distinct playable characters or every variant. Names and geometry payloads, patch merging, runtime placement, relationships and rig compatibility are unresolved. A zero-byte object is a reference/header declaration, not locally supplied geometry. Repeated UUIDs are deduplicated only for summary counts.</p>
<input id="search" aria-label="Search collection" placeholder="Search bundle or model type"><p id="count"></p>
<table><thead><tr><th>Bundle</th><th>Model types</th><th>Objects</th></tr></thead><tbody>'''+''.join(rows)+'''</tbody></table>
<script>const rows=[...document.querySelectorAll('tbody tr')],search=document.getElementById('search');function filter(){let n=0,q=search.value.toLowerCase();rows.forEach(r=>{r.hidden=!r.textContent.toLowerCase().includes(q);if(!r.hidden)n++});document.getElementById('count').textContent=n+' model-bearing bundles shown'}search.addEventListener('input',filter);filter()</script></html>'''


def write_object_pages(report, output):
    """Keep the collection index small and object pages bounded to1000 rows."""
    folder=output.with_name(output.stem+'-pages');folder.mkdir(parents=True,exist_ok=True)
    e=html.escape
    for index,bundle in enumerate(report['bundles']):
        pages=(len(bundle['objects'])+999)//1000
        bundle['browser_page']=folder.name+f'/bundle-{index:04d}-000.html'
        for page in range(pages):
            navigation=' | '.join(f'<a href="bundle-{index:04d}-{p:03d}.html">{p+1}</a>' for p in range(pages))
            rows=''.join('<tr><td>'+str(o['object_index'])+'</td><td>'+e(o['type'])+'</td><td>'+e(o['uuid_le'])+'</td><td>'+str(o['declared_serialized_bytes'])+'</td></tr>' for o in bundle['objects'][page*1000:(page+1)*1000])
            document='''<!doctype html><html lang="en"><meta charset="utf-8"><title>Model bundle resources</title><style>body{font:14px system-ui;background:#111820;color:#e1e9ef;margin:24px}a{color:#9acbff}td,th{padding:8px;border-bottom:1px solid #30404e;text-align:left}table{border-collapse:collapse;width:100%}code{overflow-wrap:anywhere}</style><a href="../'''+e(output.name,quote=True)+'''">Collection index</a><h1>Model resource identities</h1><p><code>'''+e(bundle['file'])+'''</code></p><p>Page '''+str(page+1)+'/'+str(pages)+'''. UUID identifies a serialized resource, not a playable model variant. Zero-byte declarations contain no local payload. Use browser Find to search this page.</p><p>'''+navigation+'''</p><table><tr><th>Object index</th><th>Type</th><th>UUID</th><th>Serialized payload bytes</th></tr>'''+rows+'''</table></html>'''
            (folder/f'bundle-{index:04d}-{page:03d}.html').write_text(document,encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT/'out/model-collection.json')
    parser.add_argument('--html', type=Path, default=ROOT/'out/model-collection.html')
    parser.add_argument('--lz4', type=Path, default=Path('C:/msys64/clang64/bin/liblz4.dll'))
    args = parser.parse_args()
    if args.game is None:
        sys.path.insert(0, str(ROOT/'launcher'))
        import kzsf_local
        args.game = Path(kzsf_local.load_preferences()['game_dir'])
    sys.path.insert(0, str(ROOT/'scripts'))
    from prepare import read_sfo
    info = read_sfo(args.game/'sce_sys/param.sfo')
    if (info.get('TITLE_ID'), info.get('APP_VER')) != ('CUSA00190', '01.81'):
        raise ValueError('Expected CUSA00190 / 01.81')
    spec = importlib.util.spec_from_file_location('collection_asset_reader', ROOT/'tools/inventory-kzsf-shaders.py')
    reader = importlib.util.module_from_spec(spec);spec.loader.exec_module(reader)
    decoder = reader.Lz4(args.lz4)
    files = sorted((args.game/'localcachepink/lumps').glob('*.core*'))
    if not files:
        raise ValueError('No lump files found')
    bundles, patches, failures, parsed = [], [], [], []
    identities = defaultdict(set)
    payload_identities = defaultdict(set)
    for index, path in enumerate(files):
        relative = path.relative_to(args.game).as_posix()
        if path.name.endswith('.core.patch'):
            patches.append({'file': relative, 'bytes': path.stat().st_size, 'status': 'delta listed; not parsed or merged'})
            continue
        if not path.name.endswith('.core'):
            continue
        try:
            header = model_header(path, decoder, reader.blocks)
            parsed.append(relative)
            if header['resource_objects']:
                counts = Counter(o['type'] for o in header['resource_objects'])
                bundles.append({'file': relative, 'bytes': path.stat().st_size,
                                'decoded_bytes': header['decoded_bytes'], 'header_bytes': header['header_bytes'],
                                'object_count': header['object_count'], 'model_type_counts': dict(sorted(counts.items())),
                                'objects': header['resource_objects']})
                for obj in header['resource_objects']:
                    identities[obj['type']].add(obj['uuid_le'])
                    if obj['declared_serialized_bytes']:
                        payload_identities[obj['type']].add(obj['uuid_le'])
        except (OSError, ValueError, struct.error) as error:
            failures.append({'file': relative, 'error': str(error)})
        if index % 100 == 0:
            print(f'Scanned {index+1}/{len(files)} lump entries', flush=True)
    report = {'format': 1, 'generated_utc': datetime.now(timezone.utc).isoformat(),
              'title_id': info['TITLE_ID'], 'app_version': info['APP_VER'], 'read_only': True,
              'scope': 'All localcachepink/lumps *.core base-file headers; *.core.patch identities only',
              'limits': ['No payload names/geometry/reference graph decoded', 'Patches not applied',
                         'No playable character/variant or runtime placement count', 'No rig compatibility claim'],
              'summary': {'enumerated_files': len(files), 'parsed_base_files': len(parsed), 'patch_files': len(patches),
                          'failures': len(failures), 'bundles_with_model_resources': len(bundles),
                          'unique_resource_identities': sum(map(len, identities.values())),
                          'unique_types': {key: len(values) for key, values in sorted(identities.items())},
                          'unique_with_local_payload': {key: len(values) for key, values in sorted(payload_identities.items())}},
              'parsed_files': parsed, 'patches': patches, 'failures': failures, 'bundles': bundles}
    args.output.parent.mkdir(parents=True, exist_ok=True);args.html.parent.mkdir(parents=True, exist_ok=True)
    write_object_pages(report,args.html)
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    args.html.write_text(browser(report), encoding='utf-8')
    print(json.dumps(report['summary'], indent=2));print(args.html)
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
