import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('retarget', Path(__file__).resolve().parents[1]/'tools/analyze-character-retarget.py')
retarget = importlib.util.module_from_spec(spec)
spec.loader.exec_module(retarget)

SAMPLE = '''KILLZONE_CHARACTER_RETARGET phase=begin name=Other variant=0 first_leaf_only=1 applies_pose=0
KILLZONE_CHARACTER_RETARGET phase=end status=sampled name=Other source_joints=2 destination_joints=3 matched=2 missing=1 parent_changes=1 channels=0 applies_pose=0
KILLZONE_CHARACTER_RETARGET_JOINT name=Other destination=0 source=1 parent_differs=1 joint_name_hex=68656164
KILLZONE_CHARACTER_RETARGET_JOINT name=Other destination=1 source=-1 parent_differs=0 joint_name_hex=686174
KILLZONE_CHARACTER_RETARGET_JOINT name=Other destination=2 source=0 parent_differs=0 joint_name_hex=726f6f74
'''


class ReportTests(unittest.TestCase):
    def test_direction_and_missing_names(self):
        result = retarget.parse_capture(SAMPLE)
        self.assertFalse(result['failures'])
        model = result['maps'][0]
        self.assertEqual(model['joints'][0]['source'], 1)
        self.assertEqual(model['joints'][1]['name'], 'hat')
        self.assertFalse(model['playable_verified'])
        self.assertFalse(model['applies_pose'])

    def test_truncation(self):
        result = retarget.parse_capture(SAMPLE.rsplit('KILLZONE_CHARACTER_RETARGET_JOINT', 1)[0])
        self.assertFalse(result['maps'])
        self.assertEqual(result['failures'][0]['reason'], 'incomplete-joints')

    def test_duplicate_out_of_range_and_wrong_summary(self):
        for text in (SAMPLE.replace('destination=2', 'destination=1'),
                     SAMPLE.replace('source=1 ', 'source=2 '),
                     SAMPLE.replace('matched=2', 'matched=1')):
            self.assertFalse(retarget.parse_capture(text)['maps'])

    def test_multiple_and_refused(self):
        result = retarget.parse_capture(SAMPLE+SAMPLE)
        self.assertEqual(len(result['maps']), 2)
        failed = SAMPLE.splitlines()[0]+'\nKILLZONE_CHARACTER_RETARGET phase=end status=refused name=Other reason=retarget-guard\n'
        result = retarget.parse_capture(failed)
        self.assertEqual(result['failures'][0]['reason'], 'retarget-guard')


if __name__ == '__main__':
    unittest.main()
