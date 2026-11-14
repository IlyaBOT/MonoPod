"""Rebuild actual solids for USB hood, flat lens, carrier and rigid board checks."""
import copy,json,math
import cadquery as cq
import build_enclosure as b

def main():
    p=json.loads((b.HERE/'parameters.json').read_text(encoding='utf-8')); rows=[]
    def passed(name): rows.append({'name':name,'passed':True});print('OK',name,flush=True)
    def variant(section,key,value):
        v=copy.deepcopy(p);v[section][key]=value
        m=b.Model(v).build(False);report=b.validate_assembly(m)
        assert report['passed'],(section,key,value,report)
        return m
    base=b.Model(p).build(False);report=b.validate_assembly(base);assert report['passed'],report
    passed('default_assembly_including_carrier_cable_and_installation')
    d=base.board_datums();u=base.charger_datums();j=base.jack_datums();cable=base.usb_cable_datums()
    assert p['mainboard']['width']==70 and p['mainboard']['height']==90
    assert abs(d['solder_rear']-p['wheel']['face_recess']-22)<1e-8
    assert abs(j['tip_y']+p['case']['height']/2-1)<1e-8
    assert abs(cable['usb_recess']-2)<1e-8
    assert abs(j['tip_y']-u['mouth_y']+1)<1e-8
    assert abs(u['x']-p['tp4056']['usb_width']/2-d['left']-6.5)<1e-8
    assert abs((j['x']-p['jack']['thread_diameter']/2)-(u['x']+p['tp4056']['usb_width']/2)-8)<1e-8
    passed('70x90_board_22mm_stack_protected_longer_jack_and_2mm_usb_recess')
    assert abs(cable['well_width']-10.5)<1e-8 and abs(cable['well_height']-6.4)<1e-8
    assert abs(cable['well_depth']-1.5)<1e-8 and cable['pcb_clearance']>.39
    assert report['usb_cable_fit']['passed']
    passed('measured_9p7_by_5p6_hood_and_7mm_tip_fit_with_explicit_measurement_reserve')
    model=variant('mainboard','jack_tip_recess',1.25)
    for name in base.groups['rigid_electronics']:
        change=b.bounds(model.refs[name])-b.bounds(base.refs[name])
        assert abs(change[0,1]-.25)<1e-6 and abs(change[1,1]-.25)<1e-6
        assert abs(change[:,[0,2]]).max()<1e-6
    passed('jack_protection_moves_entire_soldered_board_by_one_transform')
    model=variant('usb_cable','body_width',10.2)
    assert abs(model.usb_cable_datums()['well_width']-cable['well_width']-.5)<1e-8
    assert b.bounds(model.refs['jack_body']).tolist()==b.bounds(base.refs['jack_body']).tolist()
    passed('wider_cable_changes_well_without_moving_connectors_or_board')
    for length in [6.8,7.2]:
        variant('usb_cable','metal_tip_length',length)
        passed('tip_length_measurement_range_'+str(length))
    for section,key,value in [('tp4056','usb_tip_beyond_board',6.),('tp4056','usb_tip_beyond_board',7.),
            ('jack','tip_beyond_board',7.),('jack','tip_beyond_board',8.)]:
        model=variant(section,key,value)
        assert abs(model.jack_datums()['tip_y']+p['case']['height']/2-1)<1e-6
        passed(f'user_protrusion_range_{section}_{value:g}')
    model=variant('case','width',98.)
    assert abs(b.bounds(model.parts['back_chassis'].shape)[1,0]-49)<1e-6
    passed('case_width_changes_actual_solid')
    model=variant('display','rotation_degrees',0.)
    bb=b.bounds(model.refs['display_pcb']);assert abs(bb[1,0]-bb[0,0]-p['display']['pcb_width'])<1e-6
    passed('rotation_moves_lens_carrier_display_and_mounts_together')
    lens=base.acrylic_datums(); a=p['acrylic']; r=p['lens_carrier']; q=p['display']
    size=b.bounds(base.refs['screen_lens']);size=size[1]-size[0]
    assert all(abs(x-y)<1e-6 for x,y in zip(size,[42.,35.,1.]))
    assert abs(lens['front_depth']-.1)<1e-8 and abs(lens['rear_depth']-1.1)<1e-8
    assert abs(lens['display_air_gap']-1.5)<1e-8
    assert abs(q['pcb_front_depth']-q['glass_above_pcb']-lens['carrier_rear']-.5)<1e-8
    passed('flat_lens_is_42_by_35_by_1_recessed_point_one_with_real_air_gaps')
    def local(x,y,depth):
        angle=math.radians(q['rotation_degrees'])
        return cq.Vector(q['x']+math.cos(angle)*x-math.sin(angle)*y,q['y']+math.sin(angle)*x+math.cos(angle)*y,-depth)
    carrier=base.parts['display_lens_carrier'].shape;depth=(lens['carrier_front']+lens['carrier_rear'])/2
    assert carrier.isInside(local(lens['width']/2-.5,0,depth),1e-6)
    assert not carrier.isInside(local(lens['width']/2-r['bond_width']-.1,0,depth),1e-6)
    assert not base.parts['front_panel'].shape.isInside(local(lens['width']/2-.5,0,.5),1e-6)
    passed('two_mm_rear_bond_ring_and_full_through_panel_opening')
    x,y=base.display_mounts()[0];ear=lens['carrier_ear_front']
    assert not carrier.isInside(cq.Vector(x,y,-(ear+.4)),1e-6)
    assert carrier.isInside(cq.Vector(x-2,y,-(ear+.4)),1e-6)
    assert abs(p['fasteners']['display_screw']-q['pcb_thickness']-r['ear_thickness']-2.6)<1e-8
    passed('carrier_and_display_share_four_m25x5_screws_with_through_ears')
    variant('acrylic','thickness',1.1);passed('measured_1p1_mm_sheet_rebuilds_carrier_and_preserves_clearance')
    model=variant('acrylic','pocket_side_clearance',.3)
    probe=local(lens['width']/2+.25,0,.5)
    assert base.parts['front_panel'].shape.isInside(probe,1e-6)
    assert not model.parts['front_panel'].shape.isInside(probe,1e-6)
    passed('lens_fit_gap_changes_real_front_opening')
    jig=base.lens_gluing_jig()
    assert jig.isInside(local(0,0,.05),1e-6)
    assert not jig.isInside(local(lens['width']/2+2,0,.05),1e-6)
    passed('glue_jig_has_real_point_one_mm_lens_support_step')
    old=copy.deepcopy(p);old['acrylic']['mount_style']='rear_lip';old['fasteners']['display_screw']=4.
    model=b.Model(old).build(False);assert b.validate_assembly(model)['passed']
    assert abs(model.parts['front_panel'].shape.Volume()-base.parts['front_panel_rear_lip_alternative'].shape.Volume())<1e-6
    assert abs(model.acrylic_datums()['front_depth']-1.)<1e-8
    passed('rear_lip_alternative_remains_usable_with_m25x4_and_no_carrier')
    for name,section,key,value in [
        ('unprotected_jack','mainboard','jack_tip_recess',0.),
        ('thin_usb_web','usb_cable','well_wall',1.2),
        ('inadequate_cable_side_allowance','usb_cable','side_clearance',.1),
        ('too_short_metal_tip_hits_charger_pcb','usb_cable','metal_tip_length',6.),
        ('short_shared_display_screw','fasteners','display_screw',4.),
        ('lens_frame_hits_tft','acrylic','thickness',1.6),
        ('bond_land_too_narrow','lens_carrier','bond_width',1.),
        ('case_too_shallow','case','depth',23.),
        ('wheel_pilot_breaks_front','wheel','face_recess',0.),
        ('jack_nut_insufficient_thread','jack','panel_thickness',2.4),
    ]:
        v=copy.deepcopy(p);v[section][key]=value
        try:b.Model(v).check_parameters()
        except ValueError:passed('reject_'+name)
        else:raise AssertionError(name)
    summary={'passed':True,'checks':rows,'parameters_sha256':b.sha(b.HERE/'parameters.json'),
        'generator_sha256':b.sha(b.HERE/'build_enclosure.py'),'verifier_sha256':b.sha(b.HERE/'verify_design.py'),
        'physical_fit_verified':False,'note':'Actual geometry rebuilt. USB engagement is a provisional envelope, not electrical-contact proof.'}
    (b.ROOT/'validation/parameter_variants.json').write_text(json.dumps(summary,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print('PASS',len(rows),'parameter/constraint checks',flush=True)

if __name__=='__main__':main()
