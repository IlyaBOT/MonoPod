"""Render the actual CAD solids with a software depth buffer, no invented imagery."""
from pathlib import Path
import argparse
import json
import textwrap
import numpy as np
from PIL import Image, ImageDraw, ImageFont
import cadquery as cq
import build_enclosure as b

BG = (244, 246, 248)
INK = (27, 43, 57)
MUTED = (85, 103, 117)
ACCENT = (13, 119, 135)


def font(size, bold=False):
    # Windows host reference; DejaVu fallback makes the generator portable.
    win = Path('C:/Windows/Fonts')/('arialbd.ttf' if bold else 'arial.ttf')
    return ImageFont.truetype(str(win) if win.is_file() else ('DejaVuSans-Bold.ttf' if bold else 'DejaVuSans.ttf'), size)


def render(items, width=1200, height=950, view=(.8, -1.2, 2.7), margin=.88):
    view = np.array(view, dtype=float); view /= np.linalg.norm(view)
    right = np.cross([0, 0, 1], view)
    if np.linalg.norm(right) < 1e-8: right = np.array([1., 0, 0])
    else: right /= np.linalg.norm(right)
    up = np.cross(view, right)
    if view[2] > .999: right = np.array([1., 0, 0]); up = np.array([0, 1., 0])
    vertices, colors, alphas = [], [], []
    light = np.array([-.3, -.6, 1.3]); light /= np.linalg.norm(light)
    for shape, color in items:
        if shape.Volume() < 1e-8: continue
        vs, fs = shape.tessellate(.075, .14)
        points = np.array([v.toTuple() for v in vs]); tris = points[np.array(fs)]
        normals = np.cross(tris[:, 1]-tris[:, 0], tris[:, 2]-tris[:, 0])
        normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-12)
        alpha=color[3] if len(color)==4 else 1.
        if alpha<1:
            visible=normals@view>1e-8;tris=tris[visible];normals=normals[visible]
        shade = .54+.46*np.maximum(normals@light, 0)
        vertices.append(tris); colors.append(shade[:, None]*np.array(color[:3]));alphas.append(np.full(len(tris),alpha))
    vertices = np.concatenate(vertices); colors = np.concatenate(colors);alphas=np.concatenate(alphas)
    points = vertices@np.array([right, up, view]).T
    low, high = points[:, :, :2].min(axis=(0, 1)), points[:, :, :2].max(axis=(0, 1))
    scale = margin*min(width/(high[0]-low[0]), height/(high[1]-low[1]))
    points[:, :, :2] = (points[:, :, :2]-(low+high)/2)*scale
    points[:, :, 0] += width/2; points[:, :, 1] = height/2-points[:, :, 1]
    pixels = np.broadcast_to(np.array(BG, np.uint8), (height, width, 3)).copy()
    depth = np.full((height, width), -np.inf)
    # Opaque solids define depth first. The convex lens then blends only its
    # camera-facing surface, preserving occlusion by the front retaining lip.
    order=np.concatenate([np.where(alphas>=1)[0],np.where(alphas<1)[0]])
    for index in order:
        triangle,color,alpha=points[index],colors[index],alphas[index]
        x0,y0,z0 = triangle[0]; x1,y1,z1 = triangle[1]; x2,y2,z2 = triangle[2]
        den = (y1-y2)*(x0-x2)+(x2-x1)*(y0-y2)
        if abs(den) < 1e-9: continue
        xa, xb = max(0,int(np.floor(triangle[:,0].min()))), min(width-1,int(np.ceil(triangle[:,0].max())))
        ya, yb = max(0,int(np.floor(triangle[:,1].min()))), min(height-1,int(np.ceil(triangle[:,1].max())))
        if xa > xb or ya > yb: continue
        xx, yy = np.meshgrid(np.arange(xa,xb+1)+.5, np.arange(ya,yb+1)+.5)
        aa = ((y1-y2)*(xx-x2)+(x2-x1)*(yy-y2))/den
        bb = ((y2-y0)*(xx-x2)+(x0-x2)*(yy-y2))/den; cc = 1-aa-bb
        zz = aa*z0+bb*z1+cc*z2; old = depth[ya:yb+1,xa:xb+1]
        mask = (aa>=-1e-8)&(bb>=-1e-8)&(cc>=-1e-8)&(zz>old)
        patch=pixels[ya:yb+1,xa:xb+1]
        if alpha>=1:
            old[mask] = zz[mask];patch[mask] = np.uint8(np.clip(color*255,0,255))
        else:
            patch[mask]=np.uint8(np.clip(alpha*color*255+(1-alpha)*patch[mask],0,255))
    return Image.fromarray(pixels)


def colored(model, names):
    return [(model.parts[name].shape if name in model.parts else model.refs[name], b.COLORS.get(name,(.56,.67,.73))+((.18,) if name=='screen_lens' else ())) for name in names]


def title(image, heading, subtitle):
    result = Image.new('RGB', (image.width, image.height+115), BG)
    result.paste(image, (0,115)); draw = ImageDraw.Draw(result)
    draw.text((42,24), heading, font=font(32,True), fill=INK)
    draw.text((42,70), subtitle, font=font(20), fill=MUTED)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parameters',type=Path,default=b.HERE/'parameters.json')
    parser.add_argument('--output',type=Path,default=b.ROOT)
    args = parser.parse_args()
    root = args.output.resolve(); (root/'images').mkdir(exist_ok=True)
    model = b.Model(json.loads(args.parameters.read_text(encoding='utf-8-sig'))).build()
    p=model.p; c=p['case']; w=p['wheel']; tp=p['tp4056']; h=p['hold']
    faces = ['front_panel','back_chassis','bottom_connector_insert','hold_slider_cap','clickwheel_frame','clickwheel_wheel','clickwheel_select','clickwheel_pcb','display_pcb','display_glass','display_active','usb_c_shell','jack_body','jack_nut','screen_lens']
    if 'display_lens_carrier' in model.parts:faces.append('display_lens_carrier')
    overall = render(colored(model,faces), view=(.55,-1,3.1))
    title(overall,f'MonoPod / корпус v{p["version"]}',f'Параметрический прототип {c["width"]:g} x {c["height"]:g} x {c["depth"]:g} мм. FDM, сопло 0,4 мм.').save(root/'images/overall.png')
    rigid_names=[name for name in model.groups['rigid_electronics'] if 'keepout' not in name]
    open_names = ['back_chassis','hold_slider_cap','bottom_connector_insert']+rigid_names+['jack_nut','hold_switch_body','hold_lever']
    opened = render(colored(model,open_names),view=(.7,-1.3,2.8))
    title(opened,'Задний поддон / плата 70 x 90 мм','TP4056 и джек припаяны к макетке. Съёмная нижняя вставка, три опоры M2. Высоты осей требуют обмера.').save(root/'images/back_interior.png')
    exploded = []
    for name in dict.fromkeys(faces+rigid_names+['hold_switch_body','hold_lever']):
        shape = model.parts[name].shape if name in model.parts else model.refs[name]
        shift=0 if name in rigid_names else 26 if name=='front_panel' else -14 if name in ['back_chassis','bottom_connector_insert','hold_switch_body','hold_lever','hold_slider_cap'] else 8
        offset_y=-18 if name in ['bottom_connector_insert','jack_nut'] else 0
        color=b.COLORS.get(name,(.56,.67,.73))+((.3,) if name=='screen_lens' else ())
        exploded.append((shape.translate((0,offset_y,shift)),color))
    ex = render(exploded, width=1300, height=1100,view=(.65,-1.7,1.5))
    title(ex,'Сборка с разнесёнными деталями','Макетка, USB-C и джек остаются вместе. Нижняя вставка снимается отдельно от платы.').save(root/'images/exploded.png')
    footer = [(model.parts['bottom_edge_coupon'].shape,b.COLORS['back_chassis'])]
    clip=b.box(c['width'],p['coupons']['bottom_patch_length'],-10,c['depth']+1,0,-c['height']/2+p['coupons']['bottom_patch_length']/2)
    for name in ['bottom_connector_insert']+rigid_names+['jack_nut']:
        shape=model.parts[name].shape if name in model.parts else model.refs[name]
        footer.append((shape.intersect(clip),b.COLORS[name]))
    foot = render(footer,width=1300,height=850,view=(.55,-1.9,1.4))
    title(foot,'Нижний узел / карман кабельного штекера',f'USB-C утоплен на {model.usb_cable_datums()["usb_recess"]:.2f} мм. Носик джека защищён: {p["mainboard"]["jack_tip_recess"]:.2f} мм до наружного края.').save(root/'images/bottom_connectors.png')
    rigid_view=render(colored(model,rigid_names),width=1350,height=1000,view=(.65,-1.7,2.5))
    title(rigid_view,'Одна припаянная сборка / габаритная модель','Макетка 70 x 90; выступы USB 6,5 / джек 7,5 мм. Оси по высоте и отверстия оценочные.').save(root/'images/soldered_electronics.png')
    sequence=Image.new('RGB',(2100,1000),BG); draw=ImageDraw.Draw(sequence)
    draw.text((40,25),'УСТАНОВКА БЕЗ ВЫПАИВАНИЯ РАЗЪЁМОВ',font=font(35,True),fill=INK)
    captions=['1. Опустить всю плату в открытый поддон','2. Надеть вставку снаружи на разъёмы','3. Закрепить вставку и гайку джека']
    for index in range(3):
        items=colored(model,['back_chassis'])
        shift_z=20 if index==0 else 0
        items += [(model.refs[name].translate((0,0,shift_z)),b.COLORS[name]) for name in rigid_names]
        if index>0:
            items += [(model.parts['bottom_connector_insert'].shape.translate((0,-18 if index==1 else 0,0)),b.COLORS['bottom_connector_insert'])]
        if index==2: items += colored(model,['jack_nut'])
        tile=render(items,width=690,height=790,view=(.65,-1.8,2.0))
        sequence.paste(tile,(index*700,115))
        draw.text((20+index*700,80),captions[index],font=font(21,True),fill=INK)
    draw.text((40,940),'Плата: 3 x M2x5. Вставка: 2 x M2x4. Не подтягивать пайку гайкой при несовпадении посадки.',font=font(26),fill=INK)
    sequence.save(root/'images/installation_sequence.png')
    hold = [(model.parts['hold_switch_coupon'].shape,b.COLORS['back_chassis'])]
    hold += colored(model,['hold_slider_cap','hold_switch_body','hold_lever'])
    left = render(hold,width=700,height=600,view=(.7,1.8,1.4))
    pieces = [(model.parts['hold_switch_coupon'].shape,b.COLORS['back_chassis']),
        (model.parts['hold_slider_cap'].shape.translate((0,7,0)),b.COLORS['hold_slider_cap'])]
    pieces += [(model.refs[n].translate((0,-7,0)),b.COLORS[n]) for n in ['hold_switch_body','hold_lever']]
    right = render(pieces,width=800,height=600,view=(.75,1.6,1.1))
    hold_image = Image.new('RGB',(1500,690),BG); hold_image.paste(left,(0,45));hold_image.paste(right,(700,45))
    draw=ImageDraw.Draw(hold_image);draw.text((40,15),'В сборе',font=font(25,True),fill=INK);draw.text((760,15),'Колпачок / стенка / переключатель',font=font(25,True),fill=INK)
    title(hold_image,'HOLD / съёмная накладка с карманом','Переключатель крепится M2x4 изнутри. Размеры рычага и плотность посадки проверяются пробниками.').save(root/'images/hold_detail.png')
    # A true CAD half-section, showing the wheel/panel ring step.
    q=model.p['wheel']; cutter=b.box(120,90,-50,50,q['x'],q['y']+45)
    section=[]
    for name in ['front_panel','clickwheel_frame','clickwheel_wheel','clickwheel_select','clickwheel_pcb']:
        shape=model.parts[name].shape if name in model.parts else model.refs[name]
        near=shape.intersect(b.box(62,62,-2,12,q['x'],q['y'])).cut(cutter)
        if near.Volume()>1e-8: section.append((near,b.COLORS[name]))
    section_image=render(section,width=1400,height=600,view=(.12,1.8,.45))
    step=model.w['case_lug_front_depth']-model.w['frame_front_depth']
    title(section_image,'Посадка Click Wheel / сечение реального CAD',f'Ступень {step:.2f} + запас {w["ring_axial_clearance"]:.2f} мм. Лицо колеса утоплено на {w["face_recess"]:.2f} мм; исходная механика сохранена.').save(root/'images/wheel_interface_section.png')
    import make_detail_images as detail
    detail.generate(model,root,render,title,font,BG,INK,MUTED)
    names=[name for name,part in model.parts.items() if part.group=='coupon']
    columns=6; cell_w=360; cell_h=240; rows=(len(names)+columns-1)//columns
    sheet=Image.new('RGB',(columns*cell_w,rows*cell_h+120),BG);draw=ImageDraw.Draw(sheet)
    draw.text((35,22),f'MonoPod / {len(names)} пробников и вариантов посадки',font=font(35,True),fill=INK)
    draw.text((35,75),'Печатать выбранные файлы STL отдельно. Это обзор вариантов, а не общий файл для печати.',font=font(22),fill=MUTED)
    layouts=[]
    for index,name in enumerate(names):
        shape,_=b.printable(model.parts[name]); image=render([(shape,(.43,.61,.69))],width=cell_w-10,height=175,view=(.65,-1,2.6))
        xx=(index%columns)*cell_w; yy=(index//columns)*cell_h+120
        sheet.paste(image,(xx,yy))
        lines=textwrap.wrap(name,width=35,break_long_words=True,break_on_hyphens=False)
        for k,line in enumerate(lines[:3]): draw.text((xx+12,yy+178+k*18),line,font=font(15),fill=INK)
        bnd=b.bounds(shape); offset=((index%6)*90-bnd[0,0],(index//6)*80-bnd[0,1],0)
        layouts.append((name,shape.translate(offset)))
    sheet.save(root/'images/coupon_sheet.png')
    b.save_assembly(layouts,root/'cad/coupons_identification_sheet.step')
    # Compact cover assembled exclusively from the real CAD renders above.
    cover=Image.new('RGB',(2200,1510),BG);d=ImageDraw.Draw(cover)
    d.text((65,45),'MONOPOD / КОРПУС И ПРОБНИКИ',font=font(46,True),fill=INK)
    d.text((65,112),f'v{p["version"]}  |  акрил 1 мм + STEP + STL + DXF/SVG  |  Click Wheel v0.3',font=font(27),fill=ACCENT)
    cover.paste(overall.resize((830,657)),(25,200));cover.paste(ex.resize((960,813)),(1080,185))
    d.text((80,190),'01  ОБЩИЙ ВИД',font=font(24,True),fill=MUTED)
    d.text((1180,185),'02  КОМПОНОВКА И ДЕТАЛИ',font=font(24,True),fill=MUTED)
    cover.paste(foot.resize((880,575)),(5,900));cover.paste(hold_image.resize((1060,488)),(1080,980))
    d.text((80,890),'03  USB-C И ПАНЕЛЬНЫЙ ДЖЕК',font=font(24,True),fill=MUTED)
    d.text((1160,950),'04  HOLD: НАКЛАДКА И ПЕРЕКЛЮЧАТЕЛЬ',font=font(24,True),fill=MUTED)
    d.text((78,1420),'Сначала пробники. Размеры модулей и проводку подтвердить на вашем прототипе.',font=font(25),fill=INK)
    cover.save(root/'images/overview.png')
    manifest={'source':'Actual CadQuery geometry; software depth-buffer rendering',
        'parameters_sha256':b.sha(args.parameters),'generator_sha256':b.sha(b.HERE/'build_enclosure.py'),
        'render_script_sha256':b.sha(Path(__file__)),
        'detail_script_sha256':b.sha(b.HERE/'make_detail_images.py'),
        'images':{path.name:b.sha(path) for path in sorted((root/'images').glob('*.png')) if path.name!='laser_cut_layout.png'},'physical_fit_verified':False}
    (root/'validation/render_manifest.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print('Rendered actual CAD:',root/'images',flush=True)


if __name__=='__main__':main()
