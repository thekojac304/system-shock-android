import copy
import unittest
from unittest.mock import patch
import horror_partial as p
import horror_door_aperture as d
from test_horror_surface import tile


class PartialTests(unittest.TestCase):
    def setUp(self):
        self.tiles={y*64+x:tile(x,y) for y in range(3) for x in range(4)}

    def compile(self,source,target,face='floor',keys=None):
        bs,error=p.beams(source,self.tiles,set(),[target])
        self.assertIsNone(error)
        f=next(f for f in p.faces(1,self.tiles[target],self.tiles,keys or {}) if f[0]==face)
        status,polys,paths,reason=p.receiver(f[2],f[3],bs.get(target,[]))
        return status,polys,f

    def verify(self,source,target,result):
        status,polys,f=result;origin=f[2][0];axes=f[3]
        a,b,c=f[2][:3];normal=p.cross(p.sub(b,a),p.sub(c,a));other=next(k for k in range(3) if k not in axes)
        if status=='full':polys=[[[v[k]-origin[k] for k in axes] for v in f[2]]]
        tested=0
        for poly in polys:
            center=p.mean(poly)
            for vertex in poly:
                for frac in (.001,.1,.5,.999):
                    uv=[center[k]+frac*(vertex[k]-center[k]) for k in (0,1)]
                    point=origin[:]
                    for k,v in zip(axes,uv):point[k]+=v
                    point[other]=a[other]-sum(normal[k]*(point[k]-a[k]) for k in axes)/normal[other]
                    self.assertIsNone(p.segment_witness(source,point,self.tiles,set()),(source,point,status))
                    tested+=1
        self.assertGreater(tested,0)

    def test_full_and_blocked(self):
        result=self.compile([.5,1.5,2],64)
        self.assertEqual(result[0],'full');self.verify([.5,1.5,2],64,result)
        # Farther clear faces can conservatively retain a boundary mask when
        # floating-point clipping rounds a vertex inward. Never epsilon-expand.
        self.verify([.5,1.5,2],67,self.compile([.5,1.5,2],67))
        for y in range(3):self.tiles.pop(y*64+1)
        self.assertEqual(self.compile([.5,1.5,2],67)[0],'blocked')

    def test_partial_floor_ceiling_wall(self):
        self.tiles.pop(2);source=[.5,.25,2]
        for face in ('floor','ceiling'):
            result=self.compile(source,67,face)
            self.assertEqual(result[0],'partial');self.verify(source,67,result)
        self.tiles.pop(68,None)
        source=[.5,.5,2]
        result=self.compile(source,67,'wall',{68:0})
        self.assertEqual(result[0],'partial');self.verify(source,67,result)

    def test_diagonal_and_slope(self):
        self.tiles.pop(2);source=[.5,.25,2]
        self.tiles[67]['type']=4
        result=self.compile(source,67,'diagonal_wall')
        self.assertIn(result[0],('partial','full'));self.verify(source,67,result)
        self.tiles[67]['type']=7;self.tiles[67]['param']=8
        result=self.compile(source,67)
        self.assertEqual(result[0],'partial');self.verify(source,67,result)

    def test_all_basic_slope_mirrors_match_face_table(self):
        t=self.tiles[65];t['param']=8
        for kind in range(6,10):
            for mirror in range(4):
                t.update(type=kind,mirror=mirror)
                for face in ('floor','ceiling'):
                    for x,y in ((0,0),(1,0),(0,1),(1,1),(.23,.71)):
                        reference=p.h.edge_surface(t,0,face,x)*(1-y)+p.h.edge_surface(t,2,face,1-x)*y
                        self.assertAlmostEqual(p.height(t,face,x,y),reference)

    def test_u_shaped_and_corner_occlusion(self):
        for i in (1,65):self.tiles.pop(i)
        self.assertEqual(self.compile([.5,.5,2],2)[0],'blocked')
        self.tiles={0:tile(0,0),65:tile(1,1)}
        self.assertEqual(self.compile([.5,.5,2],65)[0],'blocked')

    def test_stability_and_bounds(self):
        self.tiles.pop(2);source=[.5,.25,2]
        a=self.compile(source,67)
        self.assertEqual(a,self.compile(source,67))
        for shift in (-1e-9,1e-9):
            b=self.compile([source[0]+shift,*source[1:]],67)
            self.assertEqual(a[0],b[0]);self.assertAlmostEqual(sum(map(p.area,a[1])),sum(map(p.area,b[1])),places=7)
        with patch.object(p,'MAX_VERTICES',2):self.assertEqual(self.compile(source,67)[0],'unsupported')
        with patch.object(p,'MAX_STATES',1):self.assertEqual(p.beams(source,self.tiles,set(),[67])[1],'beam_complexity')

    def test_nonzero_falloff_coverage(self):
        face=[[0,0,0],[1,0,0],[1,1,0],[0,1,0]]
        self.assertAlmostEqual(p.distance_to_face([.5,.5,3],face),3)
        self.assertAlmostEqual(p.distance_to_face([4,.5,4],face),5)
        self.assertAlmostEqual(p.distance_to_face([2,2,0],face),2**.5)

    def test_concave_union_never_convexified(self):
        pieces=[[[0,0],[1,0],[1,.2],[0,.2]],[[0,0],[.2,0],[.2,1],[0,1]]]
        self.assertEqual(len(p.merge_polygons(pieces)),2)
        pieces=[[[0,0],[1,0],[1,.5],[0,.5]],[[0,.5],[1,.5],[1,1],[0,1]]]
        self.assertEqual(len(p.merge_polygons(pieces)),1)

    def test_nearly_tied_hull_corners_preserve_hidden_notch(self):
        # Actual Medical T2097 / source T2034 failure, translated to local UV.
        a=[[2.84e-14,1],[.901419558,.098580442],[1,.098580442],[1,1]]
        b=[[7.1e-15,1],[2.13e-14,0],[.901419558,0],[.901419558,.098580442]]
        self.assertEqual(len(p.merge_polygons([a,b])),2)
        self.assertGreater(p.area(p.hull(a+b)),p.area(a)+p.area(b))

    def test_deterministic_profile_and_closed_door(self):
        profile=dict(level_id=1,baseline_signature='test',rules_signature='test',
            diagnostics=dict(tiles=list(self.tiles.values()),door_portals=[]),
            lights=[dict(id='auto',position=[.5,1.5,2],receiver_tiles=[67])])
        a=p.compile_profile(profile)
        self.assertEqual(a,p.compile_profile(copy.deepcopy(profile)))
        profile['diagnostics']['door_portals']=[dict(id='door',tiles=[65,66])]
        self.assertEqual(p.compile_profile(profile)['covered_tiles'],[])


class DoorTests(unittest.TestCase):
    def test_rsd_commands_and_bad_input(self):
        self.assertEqual(d.decode_rsd(bytes([0,3,7,2,8,9,130,128,0,0]),9),bytes([7,7,7,8,9,0,0,0,0]))
        self.assertEqual(d.decode_rsd(bytes([128,3,192,7,128,2,128,8,9,128,2,0,128,0,0]),7),bytes([7,7,7,8,9,0,0]))
        with self.assertRaises(ValueError):d.decode_rsd(bytes([127]),4)

    def test_actual_frame_extent_mapping(self):
        frame=dict(width=64,height=64,flags=1,pixels=bytes(4096))
        self.assertIsNone(d.aperture(frame,0))
        self.assertEqual(d.aperture(frame,3),[-31/64,-31/64,31/64,31/64])
        frame['pixels']=bytes([8])*4096
        self.assertIsNone(d.aperture(frame,7))

    def test_paired_halves_and_invalidation(self):
        portal=dict(id='P',object_ids=[1,2]);objects={i:dict(subclass=0,type=0,position=[i,0,0],heading=0) for i in (1,2)}
        models={2400:[dict(aperture=None),dict(aperture=[0,0,.4,.4])]}
        self.assertEqual(d.paired_aperture(portal,objects,models,{1:1,2:0}),[])
        self.assertEqual(len(d.paired_aperture(portal,objects,models,{1:1,2:1})),2)
        self.assertEqual(d.paired_aperture(portal,objects,models,{1:1}),[])
        deps=[['P'],[],['Q'],['P','Q']]
        self.assertEqual(d.changed_receivers(deps,{1:1,2:1},{1:0,2:1},[portal]),[0,3])
        self.assertEqual(d.changed_receivers(deps,{1:1,2:1},{1:1,2:1},[portal]),[])


if __name__=='__main__':unittest.main()
