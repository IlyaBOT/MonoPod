"""Verify current CAD evidence, write hashes and package without Python caches."""
from pathlib import Path
import csv
import hashlib
import json
import zipfile
from PIL import Image
import cadquery as cq
import build_enclosure as b


def files(root):
    return sorted(p for p in root.rglob('*') if p.is_file() and not any(part in
        {'.venv','__pycache__','work','.git'} for part in p.relative_to(root).parts) and p.suffix!='.pyc')


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def main():
    root=b.ROOT; geom=read(root/'validation/geometry_check.json'); variants=read(root/'validation/parameter_variants.json')
    renders=read(root/'validation/render_manifest.json'); manifest=read(root/'reference/input_manifest.json')
    prior=read(root/'reference/previous_revision_manifest.json');old=Path(prior['previous_project'])
    laser=read(root/'validation/laser_check.json')
    assert geom['passed'] and geom['full_roundtrip'] and geom['assembly']['passed']
    assert variants['passed'] and all(row['passed'] for row in variants['checks'])
    assert geom['parameters_sha256']==variants['parameters_sha256']==renders['parameters_sha256']==b.sha(b.HERE/'parameters.json')
    assert geom['generator_sha256']==variants['generator_sha256']==renders['generator_sha256']==b.sha(b.HERE/'build_enclosure.py')
    assert renders['render_script_sha256']==b.sha(b.HERE/'make_images.py')
    assert renders['detail_script_sha256']==b.sha(b.HERE/'make_detail_images.py')
    assert variants['verifier_sha256']==b.sha(b.HERE/'verify_design.py')
    expected=set(geom['parts'])
    assert expected=={p.stem for p in (root/'stl').glob('*.stl')},'Stale or missing STL exports'
    assert len(expected)==51 and sum(row['group']=='coupon' for row in geom['parts'].values())==45
    assert sum(row['group']=='main' for row in geom['parts'].values())==5
    assert sum(row['group']=='alternative' for row in geom['parts'].values())==1
    assert 'bottom_connector_insert' in expected and 'tp4056_clamp' not in expected
    assert not any(name.startswith('tp4056_pocket') or name.startswith('bottom_connector_pitch') for name in expected)
    assert laser['passed'] and not laser['kerf_compensation_applied']
    assert laser['parameters_sha256']==geom['parameters_sha256'] and laser['generator_sha256']==geom['generator_sha256']
    assert laser['laser_script_sha256']==b.sha(b.HERE/'make_laser_files.py')
    assert set(laser['files'])=={p.name for p in (root/'laser').iterdir() if p.is_file()}
    for name,row in laser['files'].items():assert row['passed'] and row['sha256']==b.sha(root/'laser'/name)
    assert laser['preview_sha256']==b.sha(root/'images/laser_cut_layout.png')
    with Image.open(root/'images/laser_cut_layout.png') as image:image.verify()
    for name,row in geom['parts'].items():
        assert row['passed'] and row['stl_watertight'] and row['stl_winding_consistent'] and row['step_roundtrip_passed']
        assert row['stl_sha256']==b.sha(root/'stl'/f'{name}.stl')
        assert row['step_sha256']==b.sha(root/'cad'/f'{name}.step')
        assert abs(row['stl_bounds_mm'][0][2])<1e-6
    source_status={}
    for name,row in manifest.items():
        assert b.sha(root/'reference'/name)==row['sha256']
        source=Path(row['source'])
        if source.is_file():
            assert b.sha(source)==row['sha256'],'Original input changed: '+name
            source_status[name]={'original_path_available':True,'original_hash_matches':True}
        else:
            # Clipboard/temp inputs may be cleaned between turns. Retain
            # their historical paths and verify the preserved reference
            # against BOTH the previous project and its archived bytes.
            assert b.sha(old/'reference'/name)==row['sha256']
            with zipfile.ZipFile(prior['previous_archive']) as archive:
                member=(Path(old.name)/'reference'/name).as_posix()
                assert hashlib.sha256(archive.read(member)).hexdigest()==row['sha256']
            source_status[name]={'original_path_available':False,
                'preserved_previous_reference_and_archive_match_recorded_hash':True,
                'historical_original_path':str(source)}
    for name,digest in renders['images'].items():
        path=root/'images'/name
        assert b.sha(path)==digest
        with Image.open(path) as image: image.verify()
    assembly_checks={}
    for name in ['front_with_clickwheel','back_with_connectors','assembly_mockup','coupons_identification_sheet','front_acrylic_display_assembly','acrylic_screen_lens','soldered_electronics_assembly','acrylic_carrier_subassembly','bottom_cable_fit_assembly']:
        path=root/'cad'/f'{name}.step'
        shape=cq.importers.importStep(str(path)).val()
        assert shape.isValid() and len(shape.Solids())>0
        assembly_checks[name]={'valid':True,'solids':len(shape.Solids()),'volume_mm3':shape.Volume(),'sha256':b.sha(path)}
    originals={'copied_clickwheel_files_and_four_photos_match_recorded_hashes':True,'source_status':source_status,
        'inputs':{name:value['sha256'] for name,value in manifest.items()}}
    assert b.sha(Path(prior['previous_archive']))==prior['previous_archive_sha256']
    for name,digest in prior['previous_files'].items():assert b.sha(old/name)==digest,'Previous revision changed: '+name
    prior_geom=read(old/'validation/geometry_check.json');unchanged=[];changed=[]
    expected_changes={'front_panel','back_chassis','bottom_edge_coupon','mainboard_hole_template_coupon',
        'bottom_connector_insert','display_window_coupon','acrylic_seat_coupon'}
    expected_changes.update(name for name in expected if name.startswith(('acrylic_seat_gap_','bottom_insert_gap_','bottom_aperture_extra_')))
    for name,row in prior_geom['parts'].items():
        if name not in geom['parts']:continue
        current=geom['parts'][name]
        same_volume=abs(row['volume_mm3']-current['volume_mm3'])<1e-6
        same_bounds=all(abs(x-y)<1e-6 for before,after in zip(row['cad_bounds_mm'],current['cad_bounds_mm']) for x,y in zip(before,after))
        if same_volume and same_bounds:unchanged.append(name)
        else:
            assert name in expected_changes,'Unexpected geometry change: '+name
            changed.append(name)
    old_front=cq.importers.importStep(str(old/'cad/front_panel.step')).val()
    alternative=cq.importers.importStep(str(root/'cad/front_panel_rear_lip_alternative.step')).val()
    assert abs(old_front.Volume()-alternative.Volume())<1e-6
    assert old_front.cut(alternative).Volume()+alternative.cut(old_front).Volume()<1e-5
    alt_params=read(b.HERE/'parameters_rear_lip.json');default=read(b.HERE/'parameters.json')
    wanted=read(b.HERE/'parameters.json');wanted['acrylic']['mount_style']='rear_lip';wanted['fasteners']['display_screw']=4.0
    assert alt_params==wanted
    # The lens cut files must retain their actual geometry (SVG is byte-stable;
    # DXF may contain a fresh creation timestamp/handle metadata).
    assert b.sha(root/'laser/acrylic_screen_lens_1to1.svg')==b.sha(old/'laser/acrylic_screen_lens_1to1.svg')
    assert b.sha(root/'laser/acrylic_stock_layout_1to1.svg')==b.sha(old/'laser/acrylic_stock_layout_1to1.svg')
    preservation={'previous_revision_files_and_zip_untouched':True,'unchanged_geometry_of_existing_parts':unchanged,
        'changed_existing_geometry':changed,'new_printed_parts':sorted(expected-set(prior_geom['parts'])),
        'removed_printed_parts':sorted(set(prior_geom['parts'])-expected),'acrylic_cut_svg_byte_identical':True}
    preservation['rear_lip_alternative_matches_v03_front_by_symmetric_difference']=True
    summary={'version':'0.4','part_count':len(expected),'main_parts':5,'alternative_parts':1,'coupon_parts':45,'units':'mm',
        'parameters':len(read(root/'validation/parameter_status.json')),'geometry_passed':True,'step_stl_roundtrips':len(expected),
        'parameter_checks':len(variants['checks']),'assembly_checks':assembly_checks,
        'physical_fit_verified':False,'originals':originals,'previous_revision':preservation,'laser':laser,
        'soldered_assembly_installation':geom['assembly']['installation'],
        'usb_cable_fit':geom['assembly']['usb_cable_fit'],
        'acrylic_mount':geom['assembly']['acrylic'],
        'limits':['Board 70x90 and approximate edge/protrusion/stack dimensions supplied by user; axis heights, thickness, holes and solder require measurement.',
                  'Cable hood 9.7x5.6 and exposed tip about 7 measured approximately by user. Caliper error and real USB engagement depth not established; electrical contact not verified.',
                  'Display, HOLD, nut, battery and wiring remain unmeasured. Whole-board access checked using conservative continuous envelopes; insert and cable slides checked at sample positions.',
                  'No physical FDM print, assembly, laser cut, adhesive, strength, actuation or capacitive testing performed.']}
    (root/'validation/verification_summary.json').write_text(json.dumps(summary,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    items=[path for path in files(root) if path.name!='SHA256SUMS.txt']
    (root/'SHA256SUMS.txt').write_text(''.join(f'{b.sha(path)}  {path.relative_to(root).as_posix()}\n' for path in items),encoding='utf-8')
    items=files(root); archive_path=root.with_suffix('.zip')
    with zipfile.ZipFile(archive_path,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for path in items:archive.write(path,(Path(root.name)/path.relative_to(root)).as_posix())
    with zipfile.ZipFile(archive_path) as archive:
        assert archive.testzip() is None
        for path in items:
            member=(Path(root.name)/path.relative_to(root)).as_posix()
            assert hashlib.sha256(archive.read(member)).hexdigest()==b.sha(path),member
    digest=b.sha(archive_path)
    archive_path.with_suffix('.zip.sha256').write_text(f'{digest}  {archive_path.name}\n',encoding='utf-8')
    result={'archive':str(archive_path),'bytes':archive_path.stat().st_size,'entries':len(items),'sha256':digest,
        'zip_crc_and_all_entry_sha256':'passed','printed_parts':51,'main_parts':5,'alternative_parts':1,'coupons':45,'physical_fit_verified':False,
        'laser_mm':laser['lens_mm'],'laser_units':'mm','laser_kerf_compensated':False,
        'readme':str(root/'README_RU.md'),'summary':str(root/'validation/verification_summary.json')}
    root.with_name(root.name+'_package_validation.json').write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2,ensure_ascii=False))


if __name__=='__main__':main()
