"""Inventory character asset bundles without modifying game data.

Bundle discovery is not a verified mesh/rig parse or a playable-model swap.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import html
import importlib.util
import json
from pathlib import Path
import re
import sys
from kzsf_core_index import MAX_HEADER, parse_index

ROOT = Path(__file__).resolve().parents[1]
PREFIX = 'assets_description.characters.'


def selector_support():
    """Read declared selector metadata; declaration is not visual validation."""
    source = (ROOT/'src/kzsf_character_models.h').read_text(encoding='utf-8')
    support = {}
    for name,label,bundle in re.findall(r'\{"([^"\n]+)","([^"\n]+)","([^"\n]*)"\}', source):
        if bundle:
            support.setdefault(bundle,[]).append({'resource_name':name,'label':label})
    return support


def friendly(stem):
    words = stem.removesuffix('_character').split('_')
    for n in range(len(words) // 2, 0, -1):
        if words[:n] == words[n:2*n]:
            words = words[n:]
            break
    return ' '.join(w.upper() if w in ('isa', 'vsa', 'hgh', 'dlc') else w.capitalize()
                    for w in words)


def scan_strings(path, decoder, blocks):
    references, types = set(), set()
    carry = b''
    prefix = bytearray()
    decoded_size = 0
    for offset, block in blocks(path, decoder):
        if offset != decoded_size:
            raise ValueError('Non-contiguous decoded character data')
        decoded_size += len(block)
        prefix.extend(block[:max(0, MAX_HEADER-len(prefix))])
        data = carry + block
        for match in re.finditer(rb'[ -~]{6,}', data):
            value = match.group().decode('ascii')
            if value.startswith('cache:streams/models.characters.'):
                references.add(value[:512])
            if re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*(?:Model|Mesh|Skeleton|Character|Animation)[A-Za-z0-9_]*Resource', value):
                types.add(value)
        carry = data[-1024:]
    # A .core.patch is a separate delta format, not another RTTIBin core.
    # Keep its identity/strings, but never present raw patch bytes as objects.
    if path.name.endswith('.core.patch'):
        index = dict(status='unparsed patch delta', decoded_bytes=decoded_size, patch_applied=False)
    else:
        index = parse_index(prefix, decoded_size)
    return sorted(references), sorted(types), index


def browser(catalog):
    rows = []
    for item in catalog['bundles']:
        e = html.escape
        index = (item.get('base') or {}).get('resource_index', {})
        objects = index.get('resource_objects', [])
        counts = index.get('type_counts', {})
        summary = ', '.join(f'{counts[name]} {name}' for name in
            ('SoldierResource', 'ModelPartResource', 'SkinnedModelResource', 'Skeleton') if counts.get(name))
        details = ('<details><summary>' + e(summary or 'No selected resource types') + '</summary>'
            + '<p><small>Base-file headers only. Patches and rig dependencies are unresolved.</small></p>'
            + '<ul>' + ''.join('<li>'+e(obj['type'])+' #'+str(obj['object_index'])
                +' <code>'+e(obj['uuid_le'])+'</code></li>' for obj in objects) + '</ul></details>') if index else 'Patch only; objects unresolved'
        rows.append('<tr data-search="' + e((item['name']+' '+item['id']+' '+item['category']).lower(), quote=True) + '">'
                    + '<td><strong>' + e(item['name']) + '</strong><br><code>' + e(item['id']) + '</code></td>'
                    + '<td>' + e(item['category']) + '</td><td>' + e(item['kind']) + '</td>'
                    + '<td>' + ('Base + patch' if item['base'] and item['patch'] else 'Base' if item['base'] else 'Patch only')
                    + '</td><td>' + str(len(item['character_stream_references'])) + '</td>'
                    + '<td>' + details + '</td>'
                    + '<td>' + e(item['playable_status']) + '</td></tr>')
    return '''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Killzone character catalog</title><style>
body{background:#111820;color:#e1e9ef;font:15px system-ui;margin:32px;max-width:1600px}
h1{font-size:28px;margin-bottom:8px}p{max-width:960px;color:#b8c8d5;line-height:1.6}
input{background:#1e2c39;color:white;border:1px solid #547088;border-radius:6px;padding:12px;width:min(600px,90%);margin:14px 0}
table{border-collapse:collapse;width:100%;text-align:left}th{background:#253645;position:sticky;top:0}
td,th{padding:13px;border-bottom:1px solid #30404e}code{font-size:11px;color:#9cb4c8;overflow-wrap:anywhere}
tr[hidden]{display:none}small{color:#94acbe}</style>
<h1>Killzone character catalog</h1><p>''' + str(catalog['character_bundle_count']) + ''' character bundles and ''' + str(catalog['shared_bundle_count']) + ''' shared/base bundles found in the local game. Base assets and patches are grouped together.
These are resource bundles; each can contain several model variants.</p>
<p>The selector declares '''+str(catalog['selector_alternate_count'])+''' alternate model entries plus the default player. Declared entries retain runtime compatibility checks; rendered behavior may be unverified. Other entries are collection discoveries. Base-file headers identify model parts, meshes and skeletons; patch merging, variant relationships and unlisted rig compatibility remain unresolved.</p>
<p><a href="model-collection.html" style="color:#9acbff">Browse model resources across all game bundles</a></p>
<input id="search" aria-label="Search character catalog" placeholder="Search characters, factions or internal IDs">
<p><small id="count"></small></p><table><thead><tr><th>Character / internal ID</th><th>Group</th><th>Bundle kind</th><th>Files</th><th>Character texture streams</th><th>Base resources</th><th>Play as</th></tr></thead>
<tbody>''' + ''.join(rows) + '''</tbody></table><script>
const search=document.getElementById('search'),rows=[...document.querySelectorAll('tbody tr')];
function filter(){const q=search.value.toLowerCase().trim();let count=0;for(const row of rows){row.hidden=!row.dataset.search.includes(q);if(!row.hidden)count++;}document.getElementById('count').textContent=count+' bundles shown';}
search.addEventListener('input',filter);filter();</script></html>'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT/'out/character-catalog.json')
    parser.add_argument('--html', type=Path, default=ROOT/'out/character-catalog.html')
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
        raise ValueError('This inventory expects CUSA00190 / 01.81')
    spec = importlib.util.spec_from_file_location('character_asset_reader', ROOT/'tools/inventory-kzsf-shaders.py')
    reader = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reader)
    decoder = reader.Lz4(args.lz4)
    groups, failures = {}, []
    files = sorted((args.game/'localcachepink/lumps').glob(PREFIX+'*.core*'))
    if not files:
        raise ValueError('No character bundles found under localcachepink/lumps')
    for index, path in enumerate(files):
        patch = path.name.endswith('.core.patch')
        if not patch and not path.name.endswith('.core'):
            continue
        stem = path.name[len(PREFIX):].removesuffix('.patch').removesuffix('.core')
        category = {'hgh':'Helghast', 'vsa':'VSA', 'isa':'ISA', 'coop':'Co-op', 'crowd':'Civilian'}.get(stem.split('_')[0], 'Other')
        item = groups.setdefault(stem, dict(id=stem, name=friendly(stem), category=category,
            kind='character' if stem.endswith('_character') else 'shared/base', base=None, patch=None,
            character_stream_references=[], declared_resource_types=[], playable_status='Unverified',
            rig_compatibility='unknown', individual_mesh_inventory='not parsed'))
        try:
            refs, types, resource_index = scan_strings(path, decoder, reader.blocks)
            with path.open('rb') as stream:
                digest = hashlib.file_digest(stream, 'sha256').hexdigest()
            item['patch' if patch else 'base'] = dict(file=path.relative_to(args.game).as_posix(), bytes=path.stat().st_size, sha256=digest,
                resource_index=resource_index)
            if not patch:
                item['individual_mesh_inventory'] = 'typed object headers; dependencies and patches unresolved'
            item['character_stream_references'] = sorted(set(item['character_stream_references']) | set(refs))
            item['declared_resource_types'] = sorted(set(item['declared_resource_types']) | set(types))
        except (OSError, ValueError) as error:
            failures.append(dict(file=path.name, error=str(error)))
            item['playable_status'] = 'Scan incomplete'
        if index % 20 == 0:
            print(f'Scanned {index+1}/{len(files)} asset files', flush=True)
    support = selector_support()
    for item in groups.values():
        item['selector_metadata'] = support.get(PREFIX+item['id'])
        if item['playable_status'] != 'Scan incomplete':
            item['playable_status'] = 'Declared in current selector; gameplay varies by sequence' if item['selector_metadata'] else 'Collection only; rig/animation support unverified'
    bundles = sorted(groups.values(), key=lambda item: (item['category'], item['name']))
    catalog = dict(format=2, generated_utc=datetime.now(timezone.utc).isoformat(), title_id=info['TITLE_ID'],
        app_version=info['APP_VER'], scope='all assets_description.characters core and patch bundles',
        coverage_limit='Not all embedded meshes, rigs, props, vehicles or runtime-only variants',
        playable_model_swap_implemented=True, selector_alternate_count=sum(len(entries) for entries in support.values()),
        selector_supported_bundle_count=sum(bool(b['selector_metadata']) for b in bundles),
        scanned_file_count=sum(bool(b[k]) for b in bundles for k in ('base','patch')),
        character_bundle_count=sum(b['kind']=='character' for b in bundles),
        shared_bundle_count=sum(b['kind']=='shared/base' for b in bundles), failures=failures, bundles=bundles)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.html.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(catalog, indent=2)+'\n', encoding='utf-8')
    args.html.write_text(browser(catalog), encoding='utf-8')
    print(f"{catalog['character_bundle_count']} character bundles; {catalog['shared_bundle_count']} shared/base bundles; {len(failures)} failures")
    print(args.html)
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
