"""Nominal closed DXF/SVG cut contours in mm; no guessed laser kerf offset."""
from pathlib import Path
import argparse
import json
import math
import xml.etree.ElementTree as ET
import ezdxf
from ezdxf.path import make_path
from fontTools.svgLib.path import parse_path
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.areaPen import AreaPen
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
import build_enclosure as b


def contour_points(w,h,r,x=0.,y=0.):
    quarter=math.tan(math.pi/8)
    return [(x+r,y,0),(x+w-r,y,quarter),(x+w,y+r,0),(x+w,y+h-r,quarter),
            (x+w-r,y+h,0),(x+r,y+h,quarter),(x,y+h-r,0),(x,y+r,quarter)]


def svg_path(w,h,r,x=0.,y=0.):
    return (f'M {x+r:.8f} {y:.8f} H {x+w-r:.8f} '
        f'A {r:.8f} {r:.8f} 0 0 1 {x+w:.8f} {y+r:.8f} V {y+h-r:.8f} '
        f'A {r:.8f} {r:.8f} 0 0 1 {x+w-r:.8f} {y+h:.8f} H {x+r:.8f} '
        f'A {r:.8f} {r:.8f} 0 0 1 {x:.8f} {y+h-r:.8f} V {y+r:.8f} '
        f'A {r:.8f} {r:.8f} 0 0 1 {x+r:.8f} {y:.8f} Z')


def write_dxf(path,w,h,r,positions,canvas):
    doc=ezdxf.new('R2010');doc.units=4
    doc.header['$INSUNITS']=4;doc.header['$MEASUREMENT']=1
    doc.header['$LUNITS']=2;doc.header['$LUPREC']=4
    doc.header['$LIMMIN']=(0.,0.);doc.header['$LIMMAX']=canvas
    doc.layers.new('CUT',dxfattribs={'color':1,'lineweight':0})
    for x,y in positions:
        doc.modelspace().add_lwpolyline(contour_points(w,h,r,x,y),format='xyb',close=True,dxfattribs={'layer':'CUT'})
    doc.saveas(path)


def write_svg(path,w,h,r,positions,canvas,stroke):
    ns='http://www.w3.org/2000/svg';ET.register_namespace('',ns)
    root=ET.Element('{'+ns+'}svg',{'version':'1.1','width':f'{canvas[0]:g}mm','height':f'{canvas[1]:g}mm',
        'viewBox':f'0 0 {canvas[0]:g} {canvas[1]:g}'})
    group=ET.SubElement(root,'{'+ns+'}g',{'id':'CUT','fill':'none','stroke':'#ff0000','stroke-width':str(stroke)})
    for x,y in positions:ET.SubElement(group,'{'+ns+'}path',{'d':svg_path(w,h,r,x,y)})
    ET.ElementTree(root).write(path,encoding='utf-8',xml_declaration=True)


def check_dxf(path,w,h,r,count,canvas):
    doc=ezdxf.readfile(path);audit=doc.audit();assert not audit.errors
    assert doc.header['$INSUNITS']==4 and doc.header['$MEASUREMENT']==1
    entities=list(doc.modelspace());assert len(entities)==count
    area_expected=w*h-(4-math.pi)*r*r
    outlines=[];boxes=[];unique=set()
    for entity in entities:
        assert entity.dxftype()=='LWPOLYLINE' and entity.closed and entity.dxf.layer=='CUT'
        assert len(entity)==8 and sum(abs(point[2])>1e-12 for point in entity.get_points('xyb'))==4
        path2d=make_path(entity);assert path2d.is_closed
        vertices=np.array([(v.x,v.y) for v in path2d.flattening(.0001)])
        if not np.allclose(vertices[0],vertices[-1]):vertices=np.vstack([vertices,vertices[0]])
        low,high=vertices.min(axis=0),vertices.max(axis=0)
        assert np.allclose(high-low,[w,h],atol=1e-7)
        assert np.all(low>=-1e-7) and np.all(high<=np.array(canvas)+1e-7)
        area=abs(np.sum(vertices[:-1,0]*vertices[1:,1]-vertices[1:,0]*vertices[:-1,1]))/2
        assert abs(area-area_expected)<.015
        key=tuple(np.round(low,6));assert key not in unique;unique.add(key)
        for old_low,old_high in boxes:
            assert np.any(np.minimum(old_high,high)-np.maximum(old_low,low)<=0), 'Overlapping cut outlines'
        boxes.append((low,high));outlines.append(vertices)
    return {'passed':True,'closed_cut_contours':count,'units':'mm','outline_size_mm':[w,h],
        'corner_radius_mm':r,'stock_outline_present':False,'sha256':b.sha(path)},outlines


def check_svg(path,w,h,count,canvas):
    root=ET.parse(path).getroot();ns={'s':'http://www.w3.org/2000/svg'}
    assert root.attrib['width']==f'{canvas[0]:g}mm' and root.attrib['height']==f'{canvas[1]:g}mm'
    assert root.attrib['viewBox']==f'0 0 {canvas[0]:g} {canvas[1]:g}'
    paths=root.findall('.//s:path',ns);assert len(paths)==count
    assert not root.findall('.//s:text',ns) and not root.findall('.//s:rect',ns)
    for item in paths:
        assert item.attrib['d'].strip().endswith('Z') and item.attrib['d'].count('A ')==4
        pen=BoundsPen(None);parse_path(item.attrib['d'],pen)
        x1,y1,x2,y2=pen.bounds
        assert abs(x2-x1-w)<1e-7 and abs(y2-y1-h)<1e-7
        assert x1>=-1e-7 and y1>=-1e-7 and x2<=canvas[0]+1e-7 and y2<=canvas[1]+1e-7
    return {'passed':True,'closed_cut_contours':count,'units':'mm','text_or_stock_outline_present':False,'sha256':b.sha(path)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parameters',type=Path,default=b.HERE/'parameters.json')
    parser.add_argument('--output',type=Path,default=b.ROOT)
    args=parser.parse_args();root=args.output.resolve()
    for folder in ['laser','images','validation']:(root/folder).mkdir(parents=True,exist_ok=True)
    p=json.loads(args.parameters.read_text(encoding='utf-8-sig'));model=b.Model(p)
    model.check_parameters();a=p['acrylic'];lens=model.acrylic_datums()
    # The part is only a symmetric rounded rectangle; landscape canonical
    # orientation avoids coupling laser stock axes to the display's mounting angle.
    w,h=max(lens['width'],lens['height']),min(lens['width'],lens['height']);r=a['corner_radius']
    columns=int((a['stock_width']-2*a['sheet_margin']+a['sheet_part_gap'])//(w+a['sheet_part_gap']))
    rows=int((a['stock_height']-2*a['sheet_margin']+a['sheet_part_gap'])//(h+a['sheet_part_gap']))
    assert columns>0 and rows>0
    positions=[(a['sheet_margin']+col*(w+a['sheet_part_gap']),a['sheet_margin']+row*(h+a['sheet_part_gap']))
        for row in range(rows) for col in range(columns)]
    results={};previews={}
    for name,points,canvas in [
        ('acrylic_screen_lens_1to1',[(0.,0.)],(w,h)),
        ('acrylic_stock_layout_1to1',(positions),(a['stock_width'],a['stock_height'])),
    ]:
        dxf=root/'laser'/f'{name}.dxf';svg=root/'laser'/f'{name}.svg'
        write_dxf(dxf,w,h,r,points,canvas);write_svg(svg,w,h,r,points,canvas,a['svg_stroke_width'])
        results[dxf.name],previews[name]=check_dxf(dxf,w,h,r,len(points),canvas)
        results[svg.name]=check_svg(svg,w,h,len(points),canvas)
    # Independent SVG curve integration and BRep extrusion agree on the
    # nominal outline area. SVG arcs are integrated as fontTools cubic curves.
    svg_root=ET.parse(root/'laser/acrylic_screen_lens_1to1.svg').getroot()
    path=svg_root.find('.//{http://www.w3.org/2000/svg}path')
    area_pen=AreaPen(None);parse_path(path.attrib['d'],area_pen)
    cad=model.display_shape(lens['width'],lens['height'],0,a['thickness'],r)
    cad_area=cad.Volume()/a['thickness'];expected=w*h-(4-math.pi)*r*r
    assert abs(cad_area-expected)<1e-6 and abs(abs(area_pen.value)-cad_area)<.02
    fig,axes=plt.subplots(1,2,figsize=(12,5),gridspec_kw={'width_ratios':[1,2.9]})
    fig.patch.set_facecolor('#f4f6f8')
    for ax in axes:ax.set_aspect('equal');ax.set_facecolor('#f4f6f8');ax.set_xlabel('mm');ax.set_ylabel('mm')
    for curve in previews['acrylic_screen_lens_1to1']:axes[0].plot(curve[:,0],curve[:,1],color='#bb302c',lw=1.5)
    axes[0].set_xlim(-4,w+4);axes[0].set_ylim(-4,h+4)
    axes[0].set_title(f'One lens: {w:g} x {h:g} x {a["thickness"]:g} mm\nR{r:g}; nominal contour, no kerf offset')
    for curve in previews['acrylic_stock_layout_1to1']:axes[1].plot(curve[:,0],curve[:,1],color='#bb302c',lw=1)
    axes[1].set_xlim(0,a['stock_width']);axes[1].set_ylim(0,a['stock_height']);axes[1].grid(alpha=.15)
    axes[1].set_title(f'Optional stock layout: {columns} x {rows} = {len(positions)} lenses\n{a["stock_width"]:g} x {a["stock_height"]:g} mm; stock border is not a CUT entity')
    fig.tight_layout();fig.savefig(root/'images/laser_cut_layout.png',dpi=180);plt.close(fig)
    summary={'passed':True,'version':p['version'],'units':'mm','sheet_mm':[a['stock_width'],a['stock_height'],a['thickness']],
        'lens_mm':[w,h,a['thickness']],'radius_mm':r,'one_part_file':'acrylic_screen_lens_1to1.dxf',
        'optional_sheet_parts':len(positions),'stock_margin_mm':a['sheet_margin'],'part_gap_mm':a['sheet_part_gap'],
        'kerf_compensation_applied':False,'kerf_note':'Apply measured outside compensation in CAM; do not apply it twice.',
        'cad_area_mm2':cad_area,'svg_area_mm2':abs(area_pen.value),'files':results,
        'preview_sha256':b.sha(root/'images/laser_cut_layout.png'),
        'parameters_sha256':b.sha(args.parameters),'generator_sha256':b.sha(b.HERE/'build_enclosure.py'),
        'laser_script_sha256':b.sha(Path(__file__)),'physical_cut_or_glue_test_performed':False}
    (root/'validation/laser_check.json').write_text(json.dumps(summary,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print(f'PASS laser DXF/SVG: 1 lens {w:g}x{h:g}, optional {len(positions)}-part stock layout; mm, closed true arcs, no kerf offset',flush=True)


if __name__=='__main__':main()
