"""Summarize temporary native bone maps; never mark them playable."""
import argparse
import json
from pathlib import Path


def parse_capture(text):
    maps, failures = [], []
    current = None

    def require(condition, reason):
        if not condition:
            raise ValueError(reason)

    def finish():
        if current is None:
            return
        try:
            summary = current['summary']
            require(summary.get('status') == 'sampled', summary.get('reason', 'missing-summary'))
            require(summary.get('name') == current['name'] and summary.get('applies_pose') == '0', 'summary-context')
            source, count = int(summary['source_joints']), int(summary['destination_joints'])
            require(0 < source <= 512 and 0 < count <= 512, 'joint-count')
            joints = current['joints']
            require(len(joints) == count, 'incomplete-joints')
            require([j['destination'] for j in joints] == list(range(count)), 'joint-order-or-duplicates')
            require(all(-1 <= j['source'] < source and j['parent_differs'] in (0, 1) for j in joints), 'joint-range')
            matched = sum(j['source'] >= 0 for j in joints)
            changes = sum(j['parent_differs'] for j in joints if j['source'] >= 0)
            require((matched, count-matched, changes) == tuple(int(summary[k]) for k in ('matched', 'missing', 'parent_changes')), 'summary-counts')
            maps.append(dict(name=current['name'], variant=current['variant'], source_joints=source,
                             destination_joints=count, matched=matched, missing=count-matched,
                             parent_changes=changes, channels=int(summary['channels']), joints=joints,
                             first_leaf_only=True, applies_pose=False, playable_verified=False))
        except (KeyError, ValueError, AssertionError) as error:
            failures.append(dict(name=current['name'], reason=str(error) or 'invalid-map'))

    for line in text.splitlines():
        if line.startswith('KILLZONE_CHARACTER_RETARGET '):
            fields = dict(token.split('=', 1) for token in line.split()[1:] if '=' in token)
            if fields.get('phase') == 'begin':
                finish()
                current = dict(name=fields.get('name', ''), variant=int(fields.get('variant', '-1')), joints=[])
            elif fields.get('phase') == 'end' and current is not None:
                current['summary'] = fields
        elif line.startswith('KILLZONE_CHARACTER_RETARGET_JOINT ') and current is not None:
            fields = dict(token.split('=', 1) for token in line.split()[1:] if '=' in token)
            try:
                require(fields['name'] == current['name'], 'joint-name-context')
                name_hex = fields.get('joint_name_hex', 'unreadable')
                name = None if name_hex == 'unreadable' else bytes.fromhex(name_hex).decode('utf-8', errors='replace')
                current['joints'].append(dict(destination=int(fields['destination']), source=int(fields['source']),
                                              parent_differs=int(fields['parent_differs']), name=name))
            except (ValueError, KeyError, AssertionError):
                current['summary'] = dict(status='invalid', reason='malformed-joint')
    finish()
    return dict(schema=1, maps=maps, failures=failures, applies_pose=False, playable_verified=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', required=True, type=Path)
    args = parser.parse_args()
    result = parse_capture((args.session/'stderr.log').read_text(errors='replace'))
    (args.session/'character-retarget.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(f"Native bone maps: {len(result['maps'])} complete, {len(result['failures'])} incomplete/refused; poses not applied.")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
