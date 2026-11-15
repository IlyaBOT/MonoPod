"""Actual CAD sections for the cable hood and laser-cut flat lens."""
from PIL import Image,ImageDraw
import build_enclosure as b

def generate(m,root,render,title,font,bg,ink,muted):
    p=m.p; d=m.acrylic_datums(); q=p['display']; a=p['acrylic']; carrier=m.parts['display_lens_carrier'].shape
    def get(name):return m.parts[name].shape if name in m.parts else m.refs[name]
    def color(name):return b.COLORS.get(name,(.56,.66,.72))
    sheet=Image.new('RGB',(1800,1500),bg);draw=ImageDraw.Draw(sheet)
    draw.text((45,25),'ПЛОСКИЙ АКРИЛ / СЪЁМНАЯ ВНУТРЕННЯЯ РАМКА',font=font(36,True),fill=ink)
    draw.text((45,82),'Лазерная резка 42 x 35 x 1 мм, R2. Лицо утоплено на 0,1 мм; вся ступенчатая геометрия печатается.',font=font(24),fill=muted)
    exploded=[]
    for name,shift in [('acrylic_seat_coupon',8),('screen_lens',4),('display_lens_carrier',-2),('display_pcb',-9),('display_glass',-9)]:
        col=b.COLORS['front_panel'] if name=='acrylic_seat_coupon' else color(name)
        if name=='screen_lens':col=col+(.55,)
        exploded.append((get(name).translate((0,0,shift)),col))
    view=render(exploded,width=900,height=690,view=(.6,-1.3,-1.8))
    sheet.paste(view,(0,145))
    draw.text((945,170),'Снаружи → внутрь',font=font(28,True),fill=ink)
    lines=['Акрил: D = 0,10 … 1,10 мм.','Клей по краям: 0,20 мм.','Клеевая полоса рамки: 2,00 мм.',
        'Плоская рамка: D = 1,30 … 2,10 мм.','До стекла TFT после рамки: 0,50 мм.',
        '4 общих винта M2.5x5 через TFT и рамку.','Уши рамки 0,8 + PCB 1,6 мм.',
        'Вход винта в стойку панели: 2,6 мм.','Для вклейки приложен печатный кондуктор.']
    for i,line in enumerate(lines):draw.text((945,230+i*50),line,font=font(24),fill=muted)
    scope=b.box(66,48,-1,10,q['x'],q['y']);ycut=q['y']+15
    cutter=b.box(100,100,-2,12,q['x'],ycut+50)
    section=[]
    for name in ['front_panel','screen_lens','lens_adhesive_envelope','display_lens_carrier','display_pcb','display_glass']:
        part=get(name).intersect(scope).cut(cutter)
        if part.Volume()>1e-8:section.append((part,color(name)))
    cut=render(section,width=1720,height=500,view=(.05,2.1,.33))
    draw.text((45,850),'СЕЧЕНИЕ CAD ЧЕРЕЗ КРЕПЛЕНИЯ: ПАНЕЛЬ / АКРИЛ / КЛЕЙ / РАМКА / TFT',font=font(27,True),fill=ink)
    sheet.paste(cut,(40,900))
    draw.text((45,1440),'Зазоры номинальные. Сначала пробник, рамка и один плоский акрил. Склеивать после сухой примерки.',font=font(25),fill=ink)
    sheet.save(root/'images/acrylic_mount.png')
    cv=render([(carrier,color('display_lens_carrier'))],width=1200,height=800,view=(.65,-1.2,-2.2))
    title(cv,'Внутренняя рамка / отдельная печатная деталь','Клеевая поверхность плоская. Четыре чашки обходят стойки панели; их задние уши зажимаются платой TFT.').save(root/'images/display_lens_carrier.png')
    comp=Image.new('RGB',(1800,910),bg);dc=ImageDraw.Draw(comp)
    for i,name in enumerate(['front_panel','front_panel_rear_lip_alternative']):
        shapes=[(get(name),b.COLORS['front_panel'])]
        if i==0:shapes.append((carrier,color('display_lens_carrier')))
        if i==0:shapes.append((m.refs['screen_lens'],color('screen_lens')+(.5,)))
        else:
            alt=dict(p);alt={**p,'acrylic':{**a,'mount_style':'rear_lip'}}
            old=b.Model(alt,m.root);ld=old.acrylic_datums()
            shapes.append((old.display_shape(ld['width'],ld['height'],ld['front_depth'],ld['rear_depth'],a['corner_radius']),color('screen_lens')+(.5,)))
        tile=render(shapes,width=880,height=760,view=(.5,-1.3,3))
        comp.paste(tile,(i*900,95))
    dc.text((40,25),'ВАРИАНТЫ ПЕРЕДНЕЙ ПАНЕЛИ',font=font(34,True),fill=ink)
    dc.text((40,75),'Основной: акрил 0,1 мм + задняя рамка',font=font(24),fill=muted)
    dc.text((940,75),'Альтернатива: передний бортик, акрил на 1 мм',font=font(24),fill=muted)
    comp.save(root/'images/front_variants.png')

    u=m.charger_datums(); cab=m.usb_cable_datums();outer=-p['case']['height']/2
    scope=b.box(24,32,3,24,u['x'],outer+3)
    cut_x=b.box(100,100,-1,30,u['x']+50,outer+3)
    cable_sheet=Image.new('RGB',(2100,1000),bg);dc=ImageDraw.Draw(cable_sheet)
    dc.text((40,25),'USB-C / КАРМАН ДЛЯ КОРПУСА КАБЕЛЬНОГО ШТЕКЕРА',font=font(35,True),fill=ink)
    captions=['1. Карман 10,5 x 6,4; глубина 1,5 мм','2. Кабель: корпус 9,7 x 5,6; носик 7 мм','3. Сечение по оси USB-C']
    for i in range(3):
        names=['bottom_connector_insert','usb_c_shell','jack_body','jack_nut']
        if i>0:names+=['usb_cable_hood','usb_cable_metal']
        items=[]
        for name in names:
            shape=get(name)
            if i==2:shape=shape.intersect(scope).cut(cut_x)
            if shape.Volume()>1e-8:items.append((shape,color(name)))
        tile=render(items,width=690,height=700,view=(1.7,-.9,.65) if i==2 else (.45,-2.3,1.0))
        cable_sheet.paste(tile,(i*700,165))
        dc.text((25+i*700,115),captions[i],font=font(22,True),fill=ink)
    dc.text((40,890),'Джек защищён юбкой: носик на 1 мм внутри контура. USB-C глубже наружной стенки на 2 мм.',font=font(27),fill=ink)
    dc.text((40,945),'Проверен габарит корпуса с допуском +0,2 мм и укорочением носика на 0,2 мм. Реальную посадку проверить пробником.',font=font(23),fill=muted)
    cable_sheet.save(root/'images/usb_cable_well.png')
