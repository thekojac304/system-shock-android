"""Meaningful offline safety/segmentation tests; no original assets required."""
import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
import horror_author as h


def tile(x,y,**kw):
    t=dict(id=y*64+x,x=x,y=y,type=1,floor=0.,ceiling=2.,param=0,mirror=0,
        textures={'wall':37,'floor':37,'ceiling':35},neighbor_material=False,family_bit=False,
        darkness=[3,4],transient=[0,0],fullbright=0,music=1,
        hazards={'bio':False,'radiation':False,'peril':False},flicker=0,
        safe_slab=[0.,2.],rectangular=True)
    t.update(kw);return t


def data(tiles,objects=None):
    return dict(level_id=1,signature='a'*64,tiles={t['id']:t for t in tiles},
                objects=objects or [],texture_ids=[35,37])


class InferenceTests(unittest.TestCase):
    def setUp(self):
        self.props=[dict(family_texture=0,force_dir=0,anim_group=0,group_pos=0) for _ in range(363)]
        self.rules=json.loads(h.RULES.read_text())

    def profile(self):
        return h.generate(data([tile(x,2) for x in range(2,10)]),self.props,self.rules)

    def test_unused_friend_slope_wall_slot_and_invalid_slot_rejection(self):
        base=4700
        info=bytearray(58)
        struct.pack_into('<II',info,0,64,64)
        struct.pack_into('<I',info,16,3)
        raw=bytearray(4096*16)
        offset=2109*16
        raw[offset]=7
        raw[offset+6:offset+8]=struct.pack('<H',54)
        raw[offset+9]=1  # Friend-material external walls; no slope internal wall.
        res={base+i:b'' for i in (2,4,5,7,8,19,20,22,42)}
        res[base+2]=struct.pack('<I',11)
        res[base+4]=bytes(info)
        res[base+7]=struct.pack('<54H',*range(54))
        res[base+8]=bytes(27)
        res[base+5]=bytes(raw)
        loaded=h.load_level(res,7,self.props)
        self.assertIsNone(loaded['tiles'][2109]['textures']['wall'])
        self.assertEqual(loaded['tiles'][2109]['textures']['floor'],0)
        for terrain,friend in ((7,0),(2,1)):
            raw[offset]=terrain
            raw[offset+9]=friend
            res[base+5]=bytes(raw)
            with self.assertRaisesRegex(ValueError,'Unresolved texture slot'):
                h.load_level(res,7,self.props)

    def test_closed_door_and_wall_separate_receivers(self):
        ts=[tile(x,2) for x in (2,3,4,5,6,8)]
        door=dict(id=7,tile=132,door={},position=[4.5,2.5,1],obclass=10,subclass=0,type=0,triple=0xa0000)
        p=h.generate(data(ts,[door]),self.props,self.rules)
        membership={t['id']:t['region'] for t in p['diagnostics']['tiles']}
        self.assertNotEqual(membership[131],membership[133])
        self.assertNotEqual(membership[134],membership[136])
        for light in p['lights']:
            self.assertNotIn(132,light['receiver_tiles'])
            self.assertFalse(131 in light['receiver_tiles'] and 133 in light['receiver_tiles'])

    def test_bent_same_region_does_not_illuminate_through_solid(self):
        ts=[tile(x,y) for x,y in ((2,2),(2,3),(2,4),(3,4),(4,4),(4,3),(4,2))]
        d=data(ts);r,m,_,_=h.segment(d)
        self.assertEqual(len(r),1)
        self.assertFalse(h.visible_receiver([2.5,2.5],[4.5,2.5],d['tiles'],m,r[0]['id']))
        self.assertTrue(h.visible_receiver([2.5,2.5],[2.5,4.5],d['tiles'],m,r[0]['id']))

    def test_corner_tie_and_height_occlusion(self):
        d=data([tile(2,2),tile(3,3)])
        m={i:'r' for i in d['tiles']}
        self.assertFalse(h.visible_receiver([2.5,2.5],[3.5,3.5],d['tiles'],m,'r'))
        d=data([tile(2,2),tile(3,2,floor=1.5,safe_slab=[1.5,2.]),tile(4,2)])
        m={i:'r' for i in d['tiles']}
        self.assertFalse(h.visible_receiver([2.5,2.5,1],[4.5,2.5,1],d['tiles'],m,'r'))

    def test_partial_diagonal_and_height_apertures(self):
        r,_,_,_=h.segment(data([tile(2,2),tile(3,2,type=2,rectangular=False),tile(4,2)]))
        self.assertEqual(len(r),2)  # Type 2 opens east, blocks west.
        self.assertIsNone(h.aperture(tile(2,2),tile(3,2,type=2),1))
        self.assertIsNotNone(h.aperture(tile(3,2,type=2),tile(4,2),1))
        self.assertIsNone(h.aperture(tile(2,2,ceiling=.5),tile(3,2,floor=.6),1))

    def test_darkness_retained_and_does_not_increase_light(self):
        ps=[]
        for dark in (0,8,15):
            p=h.generate(data([tile(2,2,darkness=[dark,dark])]),self.props,self.rules)
            self.assertEqual(p['diagnostics']['tiles'][0]['darkness'],[dark,dark])
            ps.append(p['lights'][0]['intensity'])
        self.assertGreater(ps[0],ps[1]);self.assertGreater(ps[1],ps[2])

    def test_low_floor_and_unknown_material_policy(self):
        for tex in ({'wall':13,'floor':35,'ceiling':13},{'wall':13,'floor':13,'ceiling':13}):
            p=h.generate(data([tile(2,2,textures=tex)]),self.props,self.rules)
            self.assertEqual(bool(p['lights']),tex['floor']==35)
            if p['lights']:self.assertLess(p['lights'][0]['intensity'],.5)
            self.assertFalse(p['defaults']['fog']['enabled'])
        p=self.profile()
        mask=next(m for m in p['material_rules'] if m['resource_id']==35)
        self.assertFalse(mask['emissive'])  # Whole-texture glow remains forbidden.
        self.assertEqual(mask['emissive_indices'],[100,101,102,103])
        self.assertNotIn(108,mask['emissive_indices'])
        wall=h.generate(data([tile(2,2,textures={'wall':35,'floor':37,'ceiling':37})]),self.props,self.rules)
        self.assertEqual(len(wall['lights']),1)
        self.assertLess(wall['lights'][0]['intensity'],.5)

    def test_repeated_fixture_energy_budget(self):
        p=self.profile();totals={}
        for light in p['lights']:
            for i in light['receiver_tiles']:totals[i]=totals.get(i,0)+light['intensity']
        self.assertTrue(totals)
        self.assertLessEqual(max(totals.values()),self.rules['generation']['max_receiver_energy']+.001)
        self.assertTrue(any(c['policy']=='suppressed_fixture_cluster' for c in p['diagnostics']['candidates']))

    def test_boundary_door_portal_closed_and_open_geometry(self):
        ts=[tile(3,y) for y in (2,3)]
        door=dict(id=9,tile=131,door={'other_half':0},position=[3.5,2.996,1.],
            heading=0,obclass=10,subclass=0,type=0,triple=0xa0000)
        d=data(ts,[door]);portals,blocked,ambiguous=h.door_portals(d)
        self.assertEqual(len(blocked),1);self.assertFalse(ambiguous)
        self.assertEqual(portals[0]['runtime_gate'],'current_frame>=3;paired_halves_all_open')
        self.assertEqual(portals[0]['panel_width'],1.0)
        self.assertEqual(portals[0]['aperture_span'],[0.,1.])
        self.assertEqual(portals[0]['plane'],2.996)
        self.assertEqual(portals[0]['edge_plane'],3)
        self.assertEqual(portals[0]['world_aperture_span'],[3.,4.])
        self.assertFalse(h.portal_transmits(portals[0],{9:2}))
        self.assertTrue(h.portal_transmits(portals[0],{9:3}))
        r,m,_,_=h.segment(d);self.assertEqual(len(r),2)
        self.assertFalse(h.visible_receiver([3.5,2.5,1],[3.5,3.5,1],d['tiles'],m,'x',ambiguous,blocked))
        self.assertTrue(h.visible_receiver([3.5,2.5,1],[3.5,3.5,1],d['tiles'],m,'x'))

    def test_door_heading_mismatch_and_paired_state_fail_closed(self):
        ts=[tile(3,y) for y in (2,3)]
        door=dict(id=9,tile=131,door={'other_half':10},position=[3.5,2.996,1.],
            heading=0,obclass=10,subclass=0,type=0,triple=0xa0000)
        partner=dict(door,id=10,door={'other_half':9})
        portal=h.door_portals(data(ts,[door,partner]))[0][0]
        self.assertFalse(h.portal_transmits(portal,{9:3,10:2}))
        self.assertTrue(h.portal_transmits(portal,{9:3,10:3}))
        door['heading']=64
        invalid=h.door_portals(data(ts,[door,partner]))[0][0]
        self.assertIsNone(invalid['edge'])
        self.assertFalse(h.portal_transmits(invalid,{9:3,10:3}))

    def test_hidden_fragment_counterexample_and_mask_signature(self):
        ts=[tile(x,y) for x in range(2,7) for y in range(2,7) if (x,y)!=(2,4)]
        d=data(ts);members={i:'r' for i in d['tiles']}
        source=[2.5,2.5,1.]
        self.assertTrue(h.visible_receiver(source,[3.5,4.5,1.],d['tiles'],members,'r'))
        self.assertFalse(h.visible_receiver(source,[3.1,4.9,1.],d['tiles'],members,'r'))
        with self.assertRaises(ValueError):h.emissive_mask_audit(b'wrong texture source',self.rules)

    def test_slope_fixture_and_text_bitmap_detection(self):
        d=data([tile(2,2,type=6,param=4,mirror=3,ceiling=2.)]);p=h.generate(d,self.props,self.rules)
        c=next(c for c in p['diagnostics']['candidates'] if c['face']=='ceiling')
        self.assertEqual(c['policy'],'local_light')
        self.assertNotEqual(c['position'][2],1.92)
        words=dict(id=21,tile=130,position=[2.5,2.5,1],obclass=7,subclass=2,type=3,
            triple=0x70203,flags=0)
        props={0x70203:{'render_type':8,'flags':0},0:{'render_type':8,'flags':0}}
        label=dict(words,id=22,triple=0,obclass=0)
        p=h.generate(data([tile(2,2)],[words,label]),self.props,self.rules,props)
        self.assertTrue(any(c['source_object_id']==21 and c['classification']=='text_bitmap_display' for c in p['diagnostics']['candidates']))
        self.assertFalse(any(c['source_object_id']==22 for c in p['diagnostics']['candidates']))

    def test_reproducible_and_bounded(self):
        a=self.profile();b=self.profile()
        self.assertEqual(h.encode(a),h.encode(b))
        self.assertLessEqual(a['diagnostics']['summary']['max_lights_per_receiver'],4)
        self.assertTrue(any(c['policy']=='suppressed_fixture_cluster' for c in a['diagnostics']['candidates']))

    def test_overlay_survives_regeneration_and_fails_closed(self):
        a=self.profile();replacement=copy.deepcopy(a['lights'][0]);replacement['intensity']=.3
        overlay=dict(schema_version=1,baseline_signature=a['baseline_signature'],patches=[
            dict(collection='lights',id=replacement['id'],replacement=replacement)])
        before=h.encode(overlay);merged=h.apply_overlay(a,overlay)
        self.assertEqual(merged['lights'][0]['intensity'],.3)
        self.assertEqual(merged['lights'][0]['origin'],'manual')
        self.assertNotEqual(a['lights'][0]['intensity'],.3)
        self.assertEqual(h.encode(h.apply_overlay(self.profile(),overlay)),h.encode(merged))
        self.assertEqual(before,h.encode(overlay))
        for alteration in ('signature','stale','nan','receiver'):
            bad=copy.deepcopy(overlay)
            if alteration=='signature':bad['baseline_signature']='b'*64
            if alteration=='stale':bad['patches'][0]['id']='missing'
            if alteration=='nan':bad['patches'][0]['replacement']['intensity']=float('nan')
            if alteration=='receiver':bad['patches'][0]['replacement']['receiver_tiles']=[4095]
            with self.assertRaises(ValueError):h.apply_overlay(a,bad)

    def test_binary_versions_and_darkness_channels(self):
        for version in (11,12):
            pad=version==12;info=bytearray(58);struct.pack_into('<II',info,0,64,64);struct.pack_into('<I',info,16,3)
            raw=bytearray(65536);i=130;t=bytearray(16);t[0]=1;t[2]=16;t[10]=0xa3;t[11]=0xb7;t[15]=0x94
            raw[i*16:i*16+16]=t;obj=bytearray((27+pad)*2);start=27+pad
            obj[start]=1;obj[start+1]=7;obj[start+2]=3
            struct.pack_into('<HH',obj,start+11+pad,640,640);obj[start+15+pad]=64
            res={4102:version.to_bytes(4,'little'),4104:info,4105:raw,4107:b'\x23\0',4108:obj,4119:b'',4120:b'',4122:b'',4142:b''}
            d=h.load_level(res,1,self.props)
            self.assertEqual(d['tiles'][i]['darkness'],[3,7]);self.assertEqual(d['tiles'][i]['transient'],[4,9])
            self.assertEqual(d['objects'][0]['position'],[2.5,2.5,1.])
            res[4105]=raw[:-1]
            with self.assertRaises(ValueError):h.load_level(res,1,self.props)

    def test_schema_rejects_invalid_defaults_and_predicates(self):
        for field,value in (('origin','mystery'),('schema_version',2),('schema_version',True)):
            p=self.profile();p[field]=value
            with self.assertRaises(ValueError):h.validate(p)
        for change in ('ambient','fog','field','dynamic'):
            p=self.profile()
            if change=='ambient':p['defaults']['ambient_intensity']=12
            if change=='fog':p['defaults']['fog']['end']=1
            if change=='field':p['lights'][0]['radius_typo']=1
            if change=='dynamic':p['lights'][0]['state_condition']={'kind':'questbit_equals','index':512,'value':True}
            with self.assertRaises(ValueError):h.validate(p)

    def test_object_property_layout_and_display_semantics(self):
        raw=bytearray(17951);struct.pack_into('<I',raw,0,45)
        # First general record: render type1, never-lit flags2<<6.
        start=len(raw)-476*27;raw[start+7]=1;struct.pack_into('<H',raw,start+20,128)
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'props.dat';path.write_bytes(raw);props=h.object_properties(path)
            self.assertEqual(props[0],{'render_type':1,'flags':128})
            obj=dict(id=7,tile=130,position=[2.5,2.5,1],obclass=0,subclass=0,type=0,triple=0,flags=0)
            p=h.generate(data([tile(2,2)],[obj]),self.props,self.rules,props)
            self.assertTrue(p['diagnostics']['objects'][0]['unlit'])
            self.assertTrue(any(c['source_object_id']==7 and c['policy']=='emissive_review' for c in p['diagnostics']['candidates']))
            path.write_bytes(raw[:-1])
            with self.assertRaises(ValueError):h.object_properties(path)


if __name__=='__main__':unittest.main()
