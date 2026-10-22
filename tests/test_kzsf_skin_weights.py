import importlib.util
from pathlib import Path
import random
import unittest

spec=importlib.util.spec_from_file_location('weights',Path(__file__).resolve().parents[1]/'tools/kzsf_skin_weights.py')
w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)


class SkinWeightTests(unittest.TestCase):
    def test_rigid_and_duplicate_joint_merge(self):
        indices,explicit=w.encode_four([3,3],[.25,.75],98)
        self.assertEqual(indices,bytes([3]*4));self.assertEqual(explicit,bytes(4))
        self.assertEqual(w.decode_four(indices,explicit,98)[1],[1,0,0,0])

    def test_quantization_conserves_weight_and_error_bound(self):
        rng=random.Random(314)
        for _ in range(2000):
            values=[rng.random() for _ in range(4)];values=[x/sum(values) for x in values]
            indices,explicit=w.encode_four([0,1,2,3],values,98)
            joints,decoded=w.decode_four(indices,explicit,98)
            self.assertAlmostEqual(sum(decoded),1)
            self.assertEqual(explicit[3],0)
            for joint,value in zip(joints,decoded):self.assertLessEqual(abs(value-values[joint]),1/255+1e-12)

    def test_eight_index_layout_matches_native_body_packing(self):
        rng=random.Random(2718)
        for count in range(1,9):
            for _ in range(300):
                values=[rng.random() for _ in range(count)];values=[x/sum(values) for x in values]
                joints=rng.sample(range(98),count)
                indices,explicit=w.encode_eight(joints,values,98)
                self.assertEqual((len(indices),len(explicit),explicit[7]),(8,8,0))
                self.assertTrue(all(a>=b for a,b in zip(explicit[:7],explicit[1:7])))      # descending
                self.assertGreaterEqual(255-sum(explicit),explicit[0])                       # slot 0 dominant
                got,decoded=w.decode_eight(indices,explicit,98);self.assertAlmostEqual(sum(decoded),1)
                dense={}
                for joint,value in zip(got,decoded):dense[joint]=dense.get(joint,0)+value
                for joint,value in zip(joints,values):self.assertLessEqual(abs(dense.get(joint,0)-value),1/255+1e-12)
        indices,explicit=w.encode_eight([7],[1.],98)
        self.assertEqual((indices,explicit),(bytes([7]+[0]*7),bytes(8)))                    # unused slots: index 0
        with self.assertRaises(ValueError):w.decode_eight(bytes(8),bytes([0]*7+[1]),98)

    def test_invalid_values_are_rejected(self):
        for joints,weights in (([98],[1]),([1],[0]),([1],[float('nan')]),([1,2],[-.1,1.1]),
                               ([0,1,2,3,4],[.2]*5)):
            with self.assertRaises(ValueError):w.encode_four(joints,weights,98)
        for indices,explicit in ((bytes(4),bytes([100,100,100,0])),(bytes([98]*4),bytes(4)),(bytes(4),bytes([0,0,0,1]))):
            with self.assertRaises(ValueError):w.decode_four(indices,explicit,98)


if __name__=='__main__':unittest.main()
