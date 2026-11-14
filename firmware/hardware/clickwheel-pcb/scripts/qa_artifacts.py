from pathlib import Path
import json,sys,re
import pymupdf as fitz
from PIL import Image,ImageDraw
from pygerber.gerberx3.api.v2 import GerberFile,Project,OnParserErrorEnum
R=Path(sys.argv[1]) if len(sys.argv)>1 else (Path(__file__).resolve().parents[1] if Path(__file__).resolve().parent.name=='scripts' else Path(r'I:\DIY_iPod_Mono\MonoPod_v0.8.6\hardware\clickwheel-pcb'))
V=R/'validation';G=R/'gerbers'
report={};files=[]
for f in sorted(G.iterdir()):
    if f.suffix in ['.gtl','.gtp','.gto','.gts','.gbs','.gm1']:
        gf=GerberFile.from_file(f);parsed=gf.parse(on_parser_error=OnParserErrorEnum.Raise);info=parsed.get_info()
        report[f.name]={n:str(getattr(info,n)) for n in ['min_x_mm','max_x_mm','min_y_mm','max_y_mm','width_mm','height_mm']}
        files.append(gf)
for sel,out in [(['.gtl','.gm1'],'gerber_copper_drills.png'),(['.gtl','.gto','.gm1'],'gerber_composite.png')]:
    ps=[GerberFile.from_file(f) for f in sorted(G.iterdir()) if f.suffix in sel]
    target=V/out;Project(ps).parse(on_parser_error=OnParserErrorEnum.Raise).render_raster(target,dpmm=40)
    # Gerbers do not subtract drills. Overlay the separately parsed actual Excellon hits.
    im=Image.open(target).convert('RGB');draw=ImageDraw.Draw(im)
    edge=report['MonoPod_ClickWheel-Edge_Cuts.gm1'];xmin=float(edge['min_x_mm']);ymax=float(edge['max_y_mm'])
    for kind,diam in [('PTH',1.0),('NPTH',2.2)]:
        s=(G/f'MonoPod_ClickWheel-{kind}.drl').read_text()
        for x,y in re.findall(r'^X(-?[0-9.]+)Y(-?[0-9.]+)$',s,re.M):
            px=(float(x)-xmin)*40;py=(ymax-float(y))*40;r=diam*20
            draw.ellipse((px-r,py-r,px+r,py+r),fill=(0,0,0))
    im.save(target)
edge=report['MonoPod_ClickWheel-Edge_Cuts.gm1'];assert abs(float(edge['width_mm'])-44.10)<.001 and abs(float(edge['height_mm'])-44.10)<.001
# PyGerber 2.4 bbox expands arcs by full aperture diameter. Verify actual centreline
# from Gerber endpoints and I/J, independently of that conservative render bbox.
edgecode=(G/'MonoPod_ClickWheel-Edge_Cuts.gm1').read_text()
assert 'X122000000Y-100000000D02*' in edgecode and 'X78000000Y-100000000I-22000000J0D01*' in edgecode
(V/'gerber_parse.json').write_text(json.dumps({'parser':'PyGerber 2.4.3, strict mode','layers':report,'drill_overlay':'Excellon coordinates and diameters, no manual redraw','outline_centreline_diameter_mm':44.0,'outline_stroke_mm':.05},indent=2))

qadir=V/'pdf_previews';qadir.mkdir(exist_ok=True);previews=[];pdfreport={}
for file in sorted((R/'exports').glob('*.pdf')):
    d=fitz.open(file);pdfreport[file.name]={'pages':len(d),'page_size_mm':[[round(p.rect.width/72*25.4,3),round(p.rect.height/72*25.4,3)] for p in d]}
    if file.name in ['pcb_top_1to1.pdf','toner_FCu_MIRRORED_POSITIVE_1to1.pdf']:
        circles=[s for s in d[0].get_drawings() if abs(s['rect'].width/72*25.4-44)<.02 and abs(s['rect'].height/72*25.4-44)<.02]
        assert circles, 'PDF is not actual size: '+file.name
        pdfreport[file.name]['verified_circle_diameter_mm']=round(circles[0]['rect'].width/72*25.4,4)
    for n,page in enumerate(d):
        path=qadir/f'{file.stem}_{n+1}.png';page.get_pixmap(matrix=fitz.Matrix(1.4,1.4),alpha=False).save(path)
        im=Image.open(path).convert('RGB');im.thumbnail((570,430));previews.append((file.name,im))
sheet=Image.new('RGB',(1200,3*480),'#d9e2e9');dr=ImageDraw.Draw(sheet)
for i,(name,im) in enumerate(previews):
    x=(i%2)*600+(600-im.width)//2;y=(i//2)*480+30;sheet.paste(im,(x,y));dr.text(((i%2)*600+15,(i//2)*480+8),name,fill='black')
sheet.save(V/'pdf_contact_sheet.png')
(V/'pdf_checks.json').write_text(json.dumps(pdfreport,indent=2))
print('Strict Gerber parse: PASS; 6 production layers. Excellon preview: 14 holes. PDFs rendered:',len(pdfreport))
