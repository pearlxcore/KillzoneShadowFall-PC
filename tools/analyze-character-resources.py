"""Correlate a completed read-only gameplay capture with base-file resource IDs."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def parse_capture(text):
    completed = []
    failed = []
    capture = part = tree = None
    soldier_name=None
    for line in text.splitlines():
        if line.startswith('KILLZONE_CHARACTER_RESOURCES begin '):
            capture = dict(parts=[], nested_parts=[], trees={}, player_current_part=None)
            part = tree = None
            soldier_name=None
        elif capture is None:
            continue
        elif match := re.fullmatch(r'  player_current_part=(\w+) variant_count=(\d+)', line):
            capture['player_current_part'] = match[1]
        elif match := re.fullmatch(r'  player_mesh=(\w+) physics=(\w+) is_skinned=([01])', line):
            capture['player_mesh'] = match[1]
        elif match := re.match(r'  loaded_parts count=(\d+) ', line):
            capture['declared_count'] = int(match[1])
            if 'source=native-query' in line:
                fields=dict(re.findall(r'(native_total|unknown_types|duplicates|truncated)=(\d+)',line))
                capture['enumeration']={key:int(value) for key,value in fields.items()}
                capture['enumeration_complete']=fields.get('truncated')=='0' and fields.get('unknown_types')=='0'
        elif match := re.fullmatch(r'  loaded_part resource=(\w+) pool=(\w+) entry_key=([0-9a-f]{32})', line):
            part = dict(resource=match[1], pool=match[2], entry_key=match[3])
            capture['parts'].append(part)
        elif match := re.fullmatch(r'  loaded_part resource=(\w+) source=native-query', line):
            part = dict(resource=match[1], source='native-query', entry_key=None)
            capture['parts'].append(part)
        elif match := re.fullmatch(r'  nested_part resource=(\w+) soldier_resource=(\w+) variant=(\d+) source=(native-query|active-player)',line):
            part=dict(resource=match[1],soldier_resource=match[2],soldier_name=soldier_name,
                      variant=int(match[3]),source=match[4],entry_key=None)
            capture['nested_parts'].append(part)
        elif line.startswith('  soldier_resource resource='):
            soldier_name=None
        elif match := re.fullmatch(r'    soldier_resource_name=(.*)',line):
            soldier_name=match[1]
        elif line.startswith('  loaded_soldiers '):
            fields=dict(re.findall(r'(count|native_total|unknown_types|truncated|player_resource_found|detail_limit)=(\d+)',line))
            capture['soldier_query']={key:int(value) for key,value in fields.items()}
            capture['soldier_query']['status']='unavailable' if 'status=unavailable' in line else 'sampled'
        elif match := re.fullmatch(r'    resource_name=(.*)', line):
            if part is not None:
                part['resource_name'] = match[1]
        elif match := re.fullmatch(r'    part_mesh=(\w+) physics=(\w+) is_skinned=([01])', line):
            if part is not None:
                part.update(mesh=match[1], physics=match[2], is_skinned=bool(int(match[3])))
        elif match := re.fullmatch(r'  mesh_tree root=(\w+) skinned_leaves=(\d+) unknown_nodes=(\d+) rig_verified=0', line):
            tree = dict(root=match[1], declared_leaves=int(match[2]), unknown_nodes=int(match[3]), leaves=[])
            capture['trees'][match[1]] = tree
        elif match := re.fullmatch(r'  mesh_tree root=(\w+) status=unavailable', line):
            tree = None
        elif match := re.fullmatch(r'    mesh_leaf mesh=(\w+) skeleton=(\w+) bindings=(\w+) bones=(\d+) animator_hash=([0-9a-f]{8}) layout_hash=([0-9a-f]{8})', line):
            if tree is not None:
                tree['leaves'].append(dict(zip(('mesh', 'skeleton', 'bindings', 'bones', 'animator_hash', 'layout_hash'), match.groups())))
        elif line.startswith('KILLZONE_CHARACTER_RESOURCES end '):
            if 'status=sampled ' in line:
                if capture.get('declared_count') != len(capture['parts']) or len(capture['parts']) > 4096:
                    raise ValueError('Incomplete loaded-part record list')
                for value in capture['trees'].values():
                    if value['declared_leaves'] != len(value['leaves']):
                        raise ValueError('Incomplete mesh leaf list')
                capture['capture_status'] = 'sampled'
                completed.append(capture)
            elif 'status=unavailable' in line:
                capture.update(capture_status='unavailable', failure=line.split('status=unavailable',1)[1].strip())
                failed.append(capture)
            capture = part = tree = None
    if not completed:
        if failed:
            return failed[-1]
        raise ValueError('No completed character resource capture; gameplay collection may not have run')
    return completed[-1]


def correlate(capture, catalog):
    identities = defaultdict(list)
    for bundle in catalog['bundles']:
        for obj in (bundle.get('base') or {}).get('resource_index', {}).get('resource_objects', []):
            if obj['type'] == 'ModelPartResource':
                identities[obj['uuid_bytes_hex']].append(dict(bundle_id=bundle['id'], name=bundle['name'],
                    object_index=obj['object_index'], uuid_le=obj['uuid_le']))
    player = next((p for p in capture['parts'] if p['resource'] == capture['player_current_part']), None)
    capture['player_part_found'] = player is not None
    player_tree = capture['trees'].get(capture.get('player_mesh') or (player or {}).get('mesh'))

    def signature(tree):
        if not tree or tree['unknown_nodes'] or not tree['leaves']:
            return None
        return {leaf['skeleton'] for leaf in tree['leaves']}

    player_skeletons = signature(player_tree)
    queried_nested=[p for p in capture.get('nested_parts',[]) if p['source']=='native-query']
    capture['queried_nested_part_count']=len(queried_nested)
    capture['player_part_found_via_soldier_query']=any(p['resource']==capture['player_current_part'] for p in queried_nested)
    capture['player_mesh_summary'] = dict(root=(player_tree or {}).get('root'),
        complete=bool(player_skeletons),skinned_sections=len((player_tree or {}).get('leaves',[])),
        skeletons=sorted(player_skeletons or []),
        binding_counts=sorted({int(leaf['bones']) for leaf in (player_tree or {}).get('leaves',[])}),
        animator_hashes=sorted({leaf['animator_hash'] for leaf in (player_tree or {}).get('leaves',[])}),
        layout_hashes=sorted({leaf['layout_hash'] for leaf in (player_tree or {}).get('leaves',[])}))
    capture['skinned_part_count']=sum(bool(part.get('is_skinned')) for part in capture['parts'])
    capture['candidate_comparison_ready']=bool(player and player_skeletons and capture.get('enumeration_complete'))
    capture['character_coverage_verified']=bool(player and capture.get('enumeration_complete'))
    capture['coverage_issue']=('Active player part absent from query; returned list is not a complete character inventory'
        if not player and capture.get('enumeration_complete') else
        'Query output truncated or partially decoded' if not capture.get('enumeration_complete') else None)
    for part in capture['parts']+capture.get('nested_parts',[]):
        part['base_header_key_matches'] = identities.get(part.get('entry_key'), [])
        part['is_player_current_part'] = part is player
        skeletons = signature(capture['trees'].get(part.get('mesh')))
        part['skeleton_relation'] = ('same-pointer-set; compatibility unverified'
            if player_skeletons and skeletons == player_skeletons else 'different or incomplete; compatibility unknown')
        part['playable_verified'] = False
    capture.update(format=1, scope='Loaded resources in one gameplay capture; base headers only, patches unresolved',
        entry_key_semantics='Opaque bytes; exact matches to base header UUID bytes are reported, no other identity inference',
        model_swapping_implemented=False, matching_part_count=sum(bool(p['base_header_key_matches']) for p in capture['parts']))
    return capture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, required=True)
    parser.add_argument('--catalog', type=Path, default=ROOT/'out/character-catalog.json')
    args = parser.parse_args()
    manifest = json.loads((args.session/'comparison.json').read_text())
    if manifest.get('diagnostic') != 'loaded-character-resources':
        raise ValueError('Session is not a loaded-character-resources diagnostic')
    capture = parse_capture((args.session/'stderr.log').read_text(errors='replace'))
    report = correlate(capture, json.loads(args.catalog.read_text(encoding='utf-8')))
    report['source_session'] = str(args.session.resolve())
    output = args.session/'character-resources.json'
    output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(f"Character capture {report['capture_status']}: {len(report['parts'])} loaded parts; {report['matching_part_count']} base-header key matches; player part found: {report['player_part_found']}")
    print(output)
    return 0 if report['capture_status']=='sampled' else 1


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, OSError) as error:
        print(f'Character resource report unavailable: {error}')
        raise SystemExit(1)
