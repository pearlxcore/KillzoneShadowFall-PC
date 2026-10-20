import importlib.util
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('character_report',ROOT/'tools/analyze-character-resources.py')
report=importlib.util.module_from_spec(spec);spec.loader.exec_module(report)

CAPTURE='''KILLZONE_CHARACTER_RESOURCES begin owner=00000001 read_only=1 native_calls=0
  player_current_part=00000002 variant_count=1
  loaded_parts count=2 manager=00000003 table=00000004 mask=7 pool_list=00000005
  loaded_part resource=00000002 pool=00000006 entry_key=11111111111111111111111111111111
    part_mesh=00000007 physics=00000008 is_skinned=1
  mesh_tree root=00000007 skinned_leaves=1 unknown_nodes=0 rig_verified=0
    mesh_leaf mesh=00000009 skeleton=0000000a bindings=0000000b bones=32 animator_hash=12345678 layout_hash=12345678
  loaded_part resource=00000012 pool=00000006 entry_key=22222222222222222222222222222222
    part_mesh=00000007 physics=00000008 is_skinned=1
KILLZONE_CHARACTER_RESOURCES end status=sampled rig_verified=0
'''


class ResourceReportTests(unittest.TestCase):
    def test_identity_match_does_not_imply_playability(self):
        catalog={'bundles':[dict(id='candidate',name='Candidate',base={'resource_index':{
            'resource_objects':[dict(type='ModelPartResource',uuid_bytes_hex='22'*16,
                                    uuid_le='display-only',object_index=3)]}})]}
        value=report.correlate(report.parse_capture(CAPTURE),catalog)
        self.assertTrue(value['player_part_found'])
        self.assertEqual(value['matching_part_count'],1)
        self.assertEqual(value['parts'][1]['base_header_key_matches'][0]['bundle_id'],'candidate')
        self.assertFalse(value['parts'][1]['playable_verified'])
        self.assertIn('unverified',value['parts'][1]['skeleton_relation'])

    def test_reject_partial_or_failed_capture(self):
        for text in (CAPTURE.rsplit('KILLZONE_CHARACTER_RESOURCES end',1)[0],
                     CAPTURE.replace('count=2','count=3'),
                     CAPTURE.replace('skinned_leaves=1','skinned_leaves=2')):
            with self.assertRaises(ValueError):report.parse_capture(text)

    def test_failed_capture_is_exported_as_failure(self):
        value=report.parse_capture(CAPTURE.replace('status=sampled rig_verified=0','status=unavailable reason=engine-context'))
        self.assertEqual(value['capture_status'],'unavailable')
        self.assertIn('engine-context',value['failure'])

    def test_native_query_has_no_invented_entry_key(self):
        text=CAPTURE.replace('pool=00000006 entry_key=11111111111111111111111111111111','source=native-query')
        value=report.correlate(report.parse_capture(text),{'bundles':[]})
        self.assertIsNone(value['parts'][0]['entry_key'])
        self.assertFalse(value['parts'][0]['playable_verified'])

    def test_complete_list_beyond_old_limit_and_truncation(self):
        text=CAPTURE.replace('count=2 manager=',
            'count=2 manager=').replace('mask=7 pool_list=00000005',
            'source=native-query native_total=922 unknown_types=0 duplicates=0 truncated=1')
        value=report.correlate(report.parse_capture(text),{'bundles':[]})
        self.assertFalse(value['enumeration_complete'])
        self.assertFalse(value['candidate_comparison_ready'])
        self.assertEqual(value['player_mesh_summary']['binding_counts'],[32])
        self.assertEqual(value['player_mesh_summary']['skinned_sections'],1)
        records=''.join(f'  loaded_part resource={i:08x} source=native-query\n' for i in range(922))
        text=('KILLZONE_CHARACTER_RESOURCES begin owner=1\n'
              '  loaded_parts count=922 source=native-query native_total=922 unknown_types=0 duplicates=0 truncated=0\n'
              +records+'KILLZONE_CHARACTER_RESOURCES end status=sampled rig_verified=0\n')
        value=report.parse_capture(text)
        self.assertEqual(len(value['parts']),922)
        self.assertTrue(value['enumeration_complete'])

    def test_player_reference_does_not_fake_query_coverage(self):
        records=('  nested_part resource=00000002 soldier_resource=1234 variant=0 source=active-player\n'
                 '    part_mesh=00000007 physics=00000008 is_skinned=1\n')
        text=CAPTURE.replace('KILLZONE_CHARACTER_RESOURCES end',records+'KILLZONE_CHARACTER_RESOURCES end')
        value=report.correlate(report.parse_capture(text),{'bundles':[]})
        self.assertFalse(value['player_part_found_via_soldier_query'])
        self.assertEqual(value['queried_nested_part_count'],0)
        text=text.replace('variant=0 source=active-player','variant=0 source=native-query')
        value=report.correlate(report.parse_capture(text),{'bundles':[]})
        self.assertTrue(value['player_part_found_via_soldier_query'])
        self.assertFalse(value['nested_parts'][0]['playable_verified'])

    def test_unknown_nodes_and_missing_player_prevent_comparison(self):
        value=report.correlate(report.parse_capture(CAPTURE.replace('unknown_nodes=0','unknown_nodes=1')),
                               {'bundles':[]})
        self.assertIn('incomplete',value['parts'][1]['skeleton_relation'])
        value=report.correlate(report.parse_capture(CAPTURE.replace('player_current_part=00000002','player_current_part=ffffffff')),
                               {'bundles':[]})
        self.assertFalse(value['player_part_found'])
        self.assertFalse(value['character_coverage_verified'])


if __name__=='__main__':unittest.main()
