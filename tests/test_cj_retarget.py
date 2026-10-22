"""Synthetic bind-fit math; no proprietary assets or live game required."""
import importlib.util
from pathlib import Path
import unittest
import numpy as np

spec=importlib.util.spec_from_file_location('retarget',Path(__file__).resolve().parents[1]/'tools/retarget-cj-model.py')
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)


class RetargetTests(unittest.TestCase):
    def test_upper_vest_clearance_is_bounded_and_does_not_move_skin_or_waist(self):
        p=np.array([[0,0,1.3],[0,0,1.45],[0,0,1.7]])
        n=np.array([[0,1,0]]*3)
        out,amount=r.garment_clearance(p,n,'white vest')
        np.testing.assert_allclose(out-p,[[0,0,0],[0,.00125,0],[0,.0025,0]],atol=1e-8)
        self.assertEqual(amount,.0025)
        skin,amount=r.garment_clearance(p,n,'hands');np.testing.assert_array_equal(skin,p)
        self.assertEqual(amount,0)
        with self.assertRaises(ValueError):r.garment_clearance(p,n*0,'white vest')

    def test_shoulder_blend_removes_helper_weight_steps_and_preserves_chest(self):
        targets=[]
        for name,x in [('shoulder_left',-.05),('upperArm_left',-.2),('lowerArm_left',-.5),
                ('shoulder_right',.05),('upperArm_right',.2),('lowerArm_right',.5),('chest',0)]:
            m=np.eye(4);m[0,3]=x;targets.append({'name':name,'world':m.T.flatten().tolist()})
        p=np.array([[.22,0,0],[.22,0,0],[-.22,0,0],[0,0,0],[.5,0,0]])
        j=np.array([[3,4,6,0],[3,4,6,0],[0,1,6,0],[6,0,0,0],[3,4,6,0]])
        w=np.array([[.1,.7,.2,0],[.7,.1,.2,0],[.1,.7,.2,0],[1,0,0,0],[.1,.7,.2,0]])
        nj,nw,report=r.fit_shoulder_weights(p,j,w,targets)
        dense=np.zeros((5,7))
        for row in range(5):
            for joint,weight in zip(nj[row],nw[row]):dense[row,joint]+=weight
        np.testing.assert_allclose(dense[0],dense[1]);np.testing.assert_allclose(dense[:,6],[.2,.2,.2,1,.2])
        self.assertAlmostEqual(dense[0,4],dense[2,1]);self.assertAlmostEqual(dense[4,4],.8)
        self.assertEqual(report['maximum_discarded_weight'],0)

    def test_chest_chain_interpolation_preserves_other_influences(self):
        targets=[]
        for name,z in [('spine3',1.3),('spine4',1.5),('chest',1.7),('shoulder_left',1.7)]:
            m=np.eye(4);m[2,3]=z;targets.append({'name':name,'world':m.T.flatten().tolist()})
        p=np.array([[0,0,z] for z in (1.2,1.4,1.6,1.8)])
        joints=np.array([[1,3,0,0]]*4);weights=np.array([[.8,.2,0,0]]*4)
        j,w,report=r.fit_chest_weights(p,joints,weights,targets)
        dense=np.zeros((4,4))
        for row in range(4):
            for joint,weight in zip(j[row],w[row]):dense[row,joint]+=weight
        np.testing.assert_allclose(dense,[[.8,0,0,.2],[.4,.4,0,.2],[0,.4,.4,.2],[0,0,.8,.2]],atol=1e-8)
        self.assertEqual(report['maximum_discarded_weight'],0)

    def test_spine_chain_uses_spine2_and_preserves_other_influences(self):
        targets=[]
        for name,z in [('spine1',1.2),('spine2',1.25),('spine3',1.35),('spine4',1.5),('chest',1.6),('shoulder_left',1.7)]:
            m=np.eye(4);m[2,3]=z;targets.append({'name':name,'world':m.T.flatten().tolist()})
        p=np.array([[0,0,z] for z in (1.1,1.225,1.30,1.55,1.8)])
        # All spine mass starts on spine1/spine3 (CJ's mapping); 20% stays on the clavicle.
        joints=np.array([[0,2,5,0]]*5);weights=np.array([[.5,.3,.2,0]]*5)
        j,w,report=r.fit_spine_chain_weights(p,joints,weights,targets)
        dense=np.zeros((5,6))
        for row in range(5):
            for joint,weight in zip(j[row],w[row]):dense[row,joint]+=weight
        np.testing.assert_allclose(dense[:,5],.2,atol=1e-8)
        np.testing.assert_allclose(dense[0],[.8,0,0,0,0,.2],atol=1e-8)
        np.testing.assert_allclose(dense[1],[.4,.4,0,0,0,.2],atol=1e-8)
        np.testing.assert_allclose(dense[2],[0,.4,.4,0,0,.2],atol=1e-8)
        np.testing.assert_allclose(dense[3],[0,0,0,.4,.4,.2],atol=1e-8)
        np.testing.assert_allclose(dense[4],[0,0,0,0,.8,.2],atol=1e-8)
        self.assertEqual(report['maximum_discarded_weight'],0)
        bad=[dict(t) for t in targets];m=np.eye(4);m[2,3]=1.0;bad[1]['world']=m.T.flatten().tolist()
        with self.assertRaises(ValueError):r.fit_spine_chain_weights(p,joints,weights,bad)

    def test_two_influence_reduction_is_exact_when_a_pair_suffices(self):
        # Bones 0 and 1 move; bone 2 stays still. A vertex weighted 0.5/0.3/0.2 where
        # bone 2 equals bone 0 in every pose must collapse to bones 0/1 at 0.7/0.3.
        poses=[]
        for dx in (0.,.1,.3):
            pal=np.repeat(np.eye(4)[None],3,axis=0);pal[1,0,3]=dx;poses.append(pal)
        palettes=np.array(poses)
        p=np.array([[0.,0,1],[0.,0,1]]);j=np.array([[0,1,2,0],[0,1,0,0]]);w=np.array([[.5,.3,.2,0],[.6,.4,0,0]])
        oj,ow,info=r.reduce_to_two_influences(p,j,w,palettes)
        self.assertEqual(info['reduced_vertices'],1);self.assertLess(info['rms_error_max_m'],1e-9)
        dense=np.zeros((2,3))
        for row in range(2):
            for joint,weight in zip(oj[row],ow[row]):dense[row,joint]+=weight
        np.testing.assert_allclose(dense[0,1],.3,atol=1e-9);np.testing.assert_allclose(dense[0].sum(),1)
        self.assertLessEqual((ow[0]>0).sum(),2);np.testing.assert_allclose(dense[1],[.6,.4,0])

    def test_deform_helper_remap_only_touches_raw_limb_joints(self):
        names=['hips','chest']+list(r.DEFORM_HELPERS)+list(r.DEFORM_HELPERS.values())
        targets=[{'name':n} for n in names];lookup={n:i for i,n in enumerate(names)}
        joints=np.array([[lookup['hips'],lookup['upperArm_left'],lookup['lowerArm_right'],lookup['upperLeg_left']]],dtype=np.uint16)
        out,info=r.remap_deform_helpers(joints,targets)
        self.assertEqual([names[x] for x in out[0]],['hips','upperArm_left_Helper1','lowerArm_right_Helper1','upperLeg_left_Helper1'])
        self.assertEqual(info['remapped_influences'],3);self.assertEqual(out.dtype,joints.dtype)
        with self.assertRaises(ValueError):r.remap_deform_helpers(joints,targets[:-1])

    def test_chest_rest_fit_is_independent_of_animation_joint(self):
        names=['Pelvis','L_Foot','L_Toe0','Spine_2']
        source=np.repeat(np.eye(4)[None],4,axis=0)
        source[:,:3,3]=[[0,1,0],[0,0,0],[.2,0,0],[0,1.3,0]]
        targets=[]
        for name,position in [('hips',[0,0,1]),('foot_left',[0,0,0]),
                ('toe_left',[0,.2,0]),('spine3',[0,0,1.3]),('spine4',[0,0,1.45])]:
            world=np.eye(4);world[:3,3]=position
            targets.append({'name':name,'world':world.T.flatten().tolist()})
        mapped,transforms,_,_=r.fit_skin(names,np.linalg.inv(source),targets)
        self.assertEqual(targets[mapped[3]]['name'],'spine4')
        pivot=transforms[3]@source[3,:,3]
        np.testing.assert_allclose(pivot,[0,0,1.3,1],atol=1e-8)

    def test_head_fit_keeps_facial_and_neck_seam_transforms_together(self):
        names=['Pelvis','L_Foot','L_Toe0','Head','HeadNub','jaw1']
        source=np.repeat(np.eye(4)[None],len(names),axis=0)
        source[:,:3,3]=[[0,1,0],[0,0,0],[.2,0,0],[0,1.7,0],[0,1.9,0],[.1,1.75,0]]
        targets=[]
        for name,position in [('hips',[0,0,1]),('foot_left',[0,0,0]),
                ('toe_left',[0,.2,0]),('neckRoot',[0,0,1.72]),('neck',[0,.03,1.8])]:
            world=np.eye(4);world[:3,3]=position
            targets.append({'name':name,'world':world.T.flatten().tolist()})
        mapped,transforms,_,_=r.fit_skin(names,np.linalg.inv(source),targets)
        self.assertEqual(targets[mapped[3]]['name'],'neckRoot')
        np.testing.assert_allclose(transforms[3],transforms[4])
        np.testing.assert_allclose(transforms[3],transforms[5])
        np.testing.assert_allclose(transforms[3]@source[3,:,3],
            [0,.03,1.8,1])

    def test_neck_blend_keeps_lower_body_and_outer_shoulders_unchanged(self):
        targets=[]
        for name,position in [('hips',[0,0,1]),('neckRoot',[0,0,1.75]),('neck',[0,0,1.83])]:
            world=np.eye(4);world[:3,3]=position;targets.append({'name':name,'world':world.T.flatten().tolist()})
        position=np.array([[0,0,1],[.25,0,1.8],[0,0,1.8],[0,-.09,1.69],
                           [0,.08,1.70],[0,.08,1.72],[0,0,1.8],[0,0,1.8]])
        joints=np.zeros((8,4),dtype=int);weights=np.array([[1.,0,0,0]]*8)
        joints[2,:2]=[0,1];weights[2,:2]=[.5,.5]
        joints[6,:2]=[2,0];weights[6,:2]=[.6,.4]
        j,w,report=r.fit_neck_weights(position,joints,weights,targets)
        for i in (0,1,4,5):self.assertEqual(j[i,0],0);self.assertAlmostEqual(w[i,0],1)
        self.assertEqual(j[2,0],1);self.assertAlmostEqual(w[2,0],1)
        self.assertEqual(set(j[3,:2]),{0,1});np.testing.assert_allclose(w.sum(axis=1),1)
        self.assertEqual(j[6,0],2);self.assertAlmostEqual(w[6,0],.6)
        self.assertEqual(j[6,1],1);self.assertAlmostEqual(w[6,1],.4)
        self.assertEqual(j[7,0],1);self.assertAlmostEqual(w[7,0],1)
        self.assertEqual(report['adjusted_vertices'],4);self.assertEqual(report['maximum_discarded_weight'],0)
        with self.assertRaises(ValueError):r.fit_neck_weights(position,joints,weights*0,targets)

    def test_body_mapping_excludes_camera_joint(self):
        self.assertNotIn('cameraJoint',r.MAP.values())
        self.assertEqual(r.MAP['Head'],r.MAP['HeadNub'])
        self.assertTrue(all(r.MAP[name]==r.MAP['Head'] for name in r.FACE))

    def test_shortest_arc_including_opposite_vectors(self):
        for a,b in (([1,0,0],[0,1,0]),([1,0,0],[-1,0,0]),([1,2,3],[1,2,3])):
            rotation=r.align(a,b);a=np.asarray(a)/np.linalg.norm(a);b=np.asarray(b)/np.linalg.norm(b)
            np.testing.assert_allclose(rotation@a,b,atol=1e-8)
            np.testing.assert_allclose(rotation.T@rotation,np.eye(3),atol=1e-8)
            self.assertAlmostEqual(np.linalg.det(rotation),1)
        with self.assertRaises(ValueError):r.align([0,0,0],[1,0,0])
        with self.assertRaises(ValueError):r.align([np.nan,0,0],[1,0,0])

    def test_weighted_translation_and_normal_normalization(self):
        transforms=np.repeat(np.eye(4)[None],2,axis=0);transforms[1,:3,3]=[2,0,0]
        p,n=r.deform(np.array([[1,0,0]]),np.array([[0,0,1]]),np.array([[0,1]]),np.array([[.25,.75]]),transforms)
        np.testing.assert_allclose(p,[[2.5,0,0]]);np.testing.assert_allclose(n,[[0,0,1]])
        for joints,weights in (([[0,2]],[[.5,.5]]),([[0,1]],[[-1,2]]),([[0,1]],[[0,0]])):
            with self.assertRaises(ValueError):r.deform(np.array([[1,0,0]]),np.array([[0,0,1]]),np.array(joints),np.array(weights),transforms)

    def test_coordinate_conversions_preserve_handedness(self):
        for matrix in (r.C,r.G):
            self.assertAlmostEqual(np.linalg.det(matrix),1)
            np.testing.assert_allclose(matrix.T@matrix,np.eye(3))
        np.testing.assert_equal(r.C@np.array([0,1,0]),[0,0,1])


if __name__=='__main__':unittest.main()
