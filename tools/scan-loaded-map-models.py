"""Read-only inventory of model/mesh resources loaded in the current map.

Asset residency does not establish placement, visibility or rig compatibility.
Instance coverage is explicitly limited to exposed-action entity links.
"""
import argparse
import csv
import html
import importlib.util
import json
from pathlib import Path
import time

spec = importlib.util.spec_from_file_location('assembly', Path(__file__).with_name('inspect-character-assembly.py'))
assembly = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assembly)


def scan(reader):
    report = {'pid': None, 'read_only': True, 'atomic': False, 'scan_epoch': time.time(),
              'scope': 'Current process loaded model/mesh resources; not all on-disk game models',
              'instance_scope': 'Partial: entities with ExposedEntityActionsComponent; not all map placements',
              'categories': {}, 'assets': [], 'instances': []}
    seen = set()
    for label, rva in [('models', 0x3c3cd00), ('parts', 0x3c3d7b0),
                       ('characters', 0x3caac10), ('meshes', 0x3b4fcf8)]:
        result = reader.registry(rva, 8192, derived=True)
        report['categories'][label] = {key: value for key, value in result.items() if key != 'objects'}
        report['categories'][label]['count'] = len(result['objects'])
        for identity in result['objects']:
            address = int(identity['address'], 16)
            if address in seen:
                continue
            seen.add(address)
            asset = {'category': label, **identity}
            if label == 'models':
                count = reader.integer(address + 0x58)
                parts = reader.pointer(address + 0x60)
                if count is not None and 0 <= count <= 128:
                    asset['part_addresses'] = [hex(reader.pointer(parts + index * 8)) for index in range(count)]
            elif label == 'parts':
                asset['mesh_address'] = hex(reader.pointer(address + 0x18))
                asset['physics_address'] = hex(reader.pointer(address + 0x28))
            elif label == 'characters':
                count = reader.integer(address + 0x648)
                variants = reader.pointer(address + 0x650)
                if count is not None and 0 <= count <= 64:
                    asset['variants'] = [{'body': hex(reader.pointer(variants + index * 240 + 8)),
                                          'face': hex(reader.pointer(variants + index * 240 + 0x18))}
                                         for index in range(count)]
            report['assets'].append(asset)
    # Most meshes are nested resources rather than standalone registry entries.
    # Follow exact known composite layouts and character face mesh links.
    by_address = {asset['address']: asset for asset in report['assets']}
    pending = []
    for asset in list(report['assets']):
        if asset['category'] == 'parts':
            pending.append((int(asset['mesh_address'], 16), asset['address'], 0))
        for variant in asset.get('variants', []):
            face = int(variant['face'], 16)
            if face and reader.identity(face).get('type_name') == 'FacialExpressionAnimatorResource':
                pending.append((reader.pointer(face + 0x30), hex(face), 0))
    visited = set()
    while pending:
        address, parent, depth = pending.pop()
        if not address:
            continue
        key = hex(address)
        if key in by_address:
            parents = by_address[key].setdefault('referenced_by', [])
            if parent not in parents:
                parents.append(parent)
        if address in visited:
            continue
        if len(visited) >= 12000:
            report['nested_mesh_truncated'] = True
            break
        visited.add(address)
        identity = reader.identity(address)
        name = identity.get('type_name', '')
        if 'MeshResource' not in name:
            report.setdefault('unknown_mesh_links', []).append(identity)
            continue
        if key not in by_address:
            asset = {'category': 'meshes', **identity, 'discovery': 'nested-reference', 'referenced_by': [parent]}
            by_address[key] = asset
            report['assets'].append(asset)
        asset = by_address[key]
        if name in ('RegularSkinnedMeshResource', 'SkinnedMeshResource'):
            skeleton = reader.pointer(address + 0x58)
            asset['skeleton'] = reader.identity(skeleton)
            asset['joint_count'] = reader.integer(skeleton + 0x30)
        stride = {'MultiMeshResource': 80, 'LodMeshResource': 16, 'SwitchMeshResource': 16}.get(name)
        if stride and depth < 9:
            count = reader.integer(address + 0x58)
            children = reader.pointer(address + 0x60)
            if count is not None and 0 <= count <= 128 and (not count or children):
                asset['children'] = [hex(reader.pointer(children + index * stride)) for index in range(count)]
                pending.extend((int(child, 16), key, depth + 1) for child in asset['children'])
            else:
                asset['children_error'] = 'Invalid bounded child array'
    report['categories']['meshes']['standalone_count'] = report['categories']['meshes']['count']
    report['categories']['meshes']['count'] = sum(asset['category'] == 'meshes' for asset in report['assets'])
    count = reader.integer(reader.base + 0x3c3b500)
    components = reader.pointer(reader.base + 0x3c3b508)
    if count is not None and 0 <= count <= 2048 and (not count or components):
        seen = set()
        for index in range(count):
            component = reader.pointer(components + index * 8)
            if reader.identity(component).get('type_name') != 'ExposedEntityActionsComponent':
                continue
            entity = reader.pointer(component + 0x38)
            if entity in seen:
                continue
            seen.add(entity)
            identity = reader.identity(entity)
            if 'rtti' not in identity:
                continue
            resource = reader.pointer(entity + 0x1d0)
            report['instances'].append({**identity, 'resource': reader.identity(resource)})
    return report


def export(report, output):
    output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    with output.with_suffix('.csv').open('w', newline='', encoding='utf-8-sig') as stream:
        writer = csv.DictWriter(stream, fieldnames=['category', 'type_name', 'resource_name', 'address', 'rtti'])
        writer.writeheader()
        for asset in report['assets']:
            writer.writerow({key: asset.get(key, '') for key in writer.fieldnames})
    esc = html.escape
    rows = []
    for asset in report['assets']:
        rig = asset.get('skeleton', {}).get('resource_name', '')
        if asset.get('joint_count') is not None:
            rig += f' ({asset["joint_count"]} joints)'
        values = [asset['category'], asset.get('type_name', ''), asset.get('resource_name') or '(unnamed)', rig, asset['address']]
        rows.append('<tr>' + ''.join('<td>' + esc(value) + '</td>' for value in values) + '</tr>')
    categories = ', '.join(f'{key}: {value["count"]}' for key, value in report['categories'].items())
    document = '''<!doctype html><meta charset="utf-8"><title>Loaded map models</title>
<style>body{font:15px system-ui;background:#111923;color:#e6edf3;margin:32px}input{padding:10px;width:65%;background:#213043;color:inherit;border:1px solid #49627e}table{border-collapse:collapse;width:100%;margin-top:20px}td,th{padding:8px;border-bottom:1px solid #334355;text-align:left}th{background:#213043;position:sticky;top:0}td:last-child{font-family:monospace}p{color:#b6c9dd}</style>
<h1>Models loaded in the current map</h1><p>''' + esc(categories) + '''</p>
<p>Read-only snapshot. Loaded assets may be unused or hidden. No model swap or rig compatibility is implied.
Entity coverage is partial; this is not a count of every placed object.</p>
<input id="filter" placeholder="Filter by character name, resource type or address" aria-label="Filter models">
<table><thead><tr><th>Category</th><th>Type</th><th>Name</th><th>Rig</th><th>Live address</th></tr></thead><tbody>''' + ''.join(rows) + '''</tbody></table>
<script>const rows=[...document.querySelectorAll('tbody tr')];document.querySelector('#filter').addEventListener('input',e=>{const q=e.target.value.toLowerCase();rows.forEach(r=>r.hidden=!r.textContent.toLowerCase().includes(q))})</script>'''
    output.with_suffix('.html').write_text(document, encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', required=True, type=int)
    parser.add_argument('--base', default=0x800000000, type=lambda value: int(value, 0))
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    reader = assembly.Inspector(args.pid, args.base)
    reader.remaining = 200000
    try:
        report = scan(reader)
        report.update(pid=args.pid, reads_used=200000-reader.remaining)
    finally:
        reader.api.CloseHandle(reader.handle)
    export(report, args.output)
    print(json.dumps({'output': str(args.output), 'assets': len(report['assets']),
                      'instances': len(report['instances']), 'categories': report['categories'],
                      'reads_used': report['reads_used']}))


if __name__ == '__main__':
    main()
