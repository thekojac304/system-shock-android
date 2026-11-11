import copy
import unittest

import horror_surface as s


def tile(x,y,kind=1):
    return dict(id=y*64+x,x=x,y=y,type=kind,height_shift=3,
                floor=0.,ceiling=4.,param=0,mirror=0)


class SurfaceReceiverTests(unittest.TestCase):
    def setUp(self):
        self.tiles={y*64+x:tile(x,y) for x in range(4) for y in range(3)}
        self.source=[.5,1.5,2.]
        self.target=self.tiles[1*64+3]

    def test_surface_identity_distinguishes_face_shape_and_location(self):
        a=s.surface_id(1,self.target,'floor')
        self.assertEqual(a,s.surface_id(1,copy.deepcopy(self.target),'floor'))
        self.assertNotEqual(a,s.surface_id(1,self.target,'ceiling'))
        changed=copy.deepcopy(self.target);changed['type']=6
        self.assertNotEqual(a,s.surface_id(1,changed,'floor'))
        changed=copy.deepcopy(self.target);changed['param']=3
        self.assertNotEqual(a,s.surface_id(1,changed,'floor'))
        self.assertNotEqual(a,s.surface_id(6,self.target,'floor'))

    def test_full_blocked_and_door_rejection(self):
        self.assertTrue(s.full_face_prism(self.source,self.target,'floor',self.tiles,set()))
        self.assertTrue(s.full_face_prism(self.source,self.target,'ceiling',self.tiles,set()))
        self.assertFalse(s.full_face_prism(self.source,self.target,'floor',self.tiles,{65}))
        self.tiles[65]['type']=0
        self.assertFalse(s.full_face_prism(self.source,self.target,'floor',self.tiles,set()))

    def test_partial_visibility_rejected_as_whole_face(self):
        # A ray to the center can pass below the solid cell, while a corner
        # ray intersects it. The bounding-prism proof rejects the whole face.
        self.tiles[2]['type']=0
        self.assertFalse(s.full_face_prism([.5,.25,2.],self.target,'floor',self.tiles,set()))

    def test_height_and_complex_face_rejected(self):
        self.tiles[65]['floor']=2.1
        self.assertFalse(s.full_face_prism(self.source,self.target,'floor',self.tiles,set()))
        self.tiles[65]['floor']=0.
        self.target['type']=6
        self.assertFalse(s.full_face_prism(self.source,self.target,'floor',self.tiles,set()))

    def test_compilation_deterministic_and_closed_portal(self):
        p={'level_id':1,'baseline_signature':'fixture','rules_signature':'rules',
           'diagnostics':{'tiles':list(self.tiles.values()),
                          'door_portals':[{'tiles':[65,66]}]},
           'lights':[{'id':'l','position':self.source,'receiver_tiles':[self.target['id']]}]}
        a=s.compile_surface_receivers(p)
        self.assertEqual(a,s.compile_surface_receivers(copy.deepcopy(p)))
        self.assertEqual(a['counts']['full'],0)
        p['diagnostics']['door_portals']=[]
        self.assertEqual(s.compile_surface_receivers(p)['counts']['full'],2)


if __name__=='__main__':unittest.main()
