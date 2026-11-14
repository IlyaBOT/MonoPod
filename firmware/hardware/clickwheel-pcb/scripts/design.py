from pathlib import Path
import re,json,copy,uuid,math,shutil,os

ROOT=Path(os.environ.get('CLICKWHEEL_ROOT',str(Path(__file__).resolve().parents[1]) if Path(__file__).resolve().parent.name=='scripts' else r'I:\DIY_iPod_Mono\MonoPod_v0.8.6\hardware\clickwheel-pcb'))
NAME='MonoPod_ClickWheel'
KICAD=Path(r'C:\Program Files\KiCad\10.0\share\kicad')
for d in ['libraries/ClickWheel.pretty','libraries/models','docs/sources','scripts','validation','gerbers','renders','exports']:(ROOT/d).mkdir(parents=True,exist_ok=True)
class Atom(str):pass
def parse(s):
    stack=[];result=None
    for t in re.findall(r'"(?:\\.|[^"\\])*"|[^\s()]+|[()]',s):
        if t=='(':
            a=[]
            if stack:stack[-1].append(a)
            stack.append(a)
        elif t==')':result=stack.pop()
        else:stack[-1].append(json.loads(t) if t.startswith('"') else Atom(t))
    return result
def dump(a):
    if isinstance(a,list):return '('+' '.join(dump(x) for x in a)+')'
    if isinstance(a,Atom):return str(a)
    return json.dumps(a,ensure_ascii=False)
def child(a,k):return next((x for x in a if isinstance(x,list) and x and x[0]==k),None)
def kids(a,k):return [x for x in a if isinstance(x,list) and x and x[0]==k]
def uid(s):return str(uuid.uuid5(uuid.NAMESPACE_URL,'monopod-clickwheel-r1/'+s))
def q(s):return json.dumps(str(s),ensure_ascii=False)
SYMS={}
def addlib(lib,name):
    key=lib+':'+name
    ss={s[1]:s for s in kids(parse((KICAD/'symbols'/f'{lib}.kicad_sym').read_text(encoding='utf-8')),'symbol')}
    def flat(n):
        s=copy.deepcopy(ss[n]);ex=child(s,'extends')
        if ex:
            parent=flat(ex[1]);s.remove(ex);own={x[1] for x in kids(s,'property')}
            s+=copy.deepcopy([x for x in parent[2:] if not(isinstance(x,list) and x[0]=='property' and x[1] in own)])
            for sub in kids(s,'symbol'):sub[1]=str(sub[1]).replace(ex[1]+'_',n+'_')
        return s
    s=flat(name);s[1]=key;SYMS[key]=s
for lib,n in [('Device','R'),('Device','C'),('Switch','SW_Push'),('Connector_Generic','Conn_02x05_Odd_Even'),('Connector_Generic','Conn_01x01'),('Mechanical','MountingHole'),('power','PWR_FLAG')]:addlib(lib,n)
COMP={}
def comp(ref,key,value,fp,nets,x,y,rot=0,role='',mpn=''):
    COMP[ref]=dict(ref=ref,key=key,value=value,fp='ClickWheel:'+fp,nets={str(k):v for k,v in nets.items()},x=x,y=y,rot=rot,uuid=uid(ref),role=role,mpn=mpn)
for i,(v,n,x,y) in enumerate([('MENU','MENU_RET',0,-16.5),('PREV','PREV_RET',-16.5,0),('NEXT','NEXT_RET',16.5,0),('PLAY','PLAY_RET',0,16.5),('SELECT','SELECT_RET',0,0)],1):
    comp('SW'+str(i),'Switch:SW_Push',v,'TS1187A_4x4x0p8_REF',{1:'BTN_ADC',2:n},x,y,role='User kit 4x4x0.8; verify against 6.0x3.7mm pad centres; logical pin1=physical A/B, pin2=C/D',mpn='TS-1187A 4x4x0.8 (kit, manufacturer unconfirmed)')
for i,(v,a,b,x,y,rot,mpn) in enumerate([
('10k','3V3','BTN_ADC',-4,13,0,'RC0603FR-0710KL'),
('0R','MENU_RET','GND',0,-12,270,'RC0603JR-070RL'),
('2.2k','PREV_RET','GND',-11,1.85,0,'RC0603FR-072K2L'),
('4.7k','NEXT_RET','GND',11,1.85,180,'RC0603FR-074K7L'),
('10k','PLAY_RET','GND',8,16,0,'RC0603FR-0710KL'),
('22k','SELECT_RET','GND',6,2,0,'RC0603FR-0722KL')],1):comp('R'+str(i),'Device:R',v,'R_0603',{1:a,2:b},x,y,rot,role='1%, 0603 (0R jumper tolerance not applicable)' if i!=2 else 'Required MENU 0 ohm link',mpn=mpn)
comp('C1','Device:C','100n','C_0603',{1:'BTN_ADC',2:'GND'},-8,8,0,'X7R 50V +/-10%','GRM188R71H104KA93D')
pins={1:'3V3',2:'GND',3:'BTN_ADC',4:'TOUCH1',5:'TOUCH2',6:'TOUCH3',7:'TOUCH4',8:'TOUCH5',9:'TOUCH6',10:None}
comp('J1','Connector_Generic:Conn_02x05_Odd_Even','WIRE / 2x05 P2.54','Header_2x05_P2p54_TopOnly',pins,-5.08,11,90,'Bare plated holes; no connector populated; optional header from underside')
for i,(x,y) in enumerate([(-4.5,6),(0,13),(0,6),(2.54,13),(4.5,6),(5.08,13)],1):comp('E'+str(i),'Connector_Generic:Conn_01x01','TOUCH'+str(i),'TouchWire_2x1p5',{1:'TOUCH'+str(i)},x,y,role='Fine flexible electrode wire, exposed front land')
for i,(x,y) in enumerate([(-13,-13),(13,-13),(-13,13),(13,13)],1):comp('H'+str(i),'Mechanical:MountingHole','M2 / 2.2 NPTH','Mount_M2_Keepout5',{},x,y,role='M2 screw head/washer envelope diameter 5.0mm')

def libraries():
    # Portable subset symbols, so global library updates do not change the project.
    for lib in sorted(set(k.split(':')[0] for k in SYMS)):
        sy=[]
        for key,s in SYMS.items():
            if key.split(':')[0]==lib:
                c=copy.deepcopy(s);c[1]=key.split(':')[1];sy.append(dump(c))
        (ROOT/'libraries'/f'{lib}.kicad_sym').write_text('(kicad_symbol_lib (version 20241209) (generator "kicad_symbol_editor") '+ ' '.join(sy)+')',encoding='utf-8')
    fps=ROOT/'libraries/ClickWheel.pretty'
    for lib,src,name in [('Resistor_SMD','R_0603_1608Metric','R_0603'),('Capacitor_SMD','C_0603_1608Metric','C_0603'),('Connector_PinHeader_2.54mm','PinHeader_2x05_P2.54mm_Vertical','Header_2x05_P2p54_TopOnly')]:
        s=parse((KICAD/'footprints'/f'{lib}.pretty'/f'{src}.kicad_mod').read_text(encoding='utf-8'));s[1]=name
        if name.startswith('Header'):
            for p in kids(s,'pad'):
                child(p,'layers')[:]=[Atom('layers'),'F.Cu','*.Mask']
                r=child(p,'remove_unused_layers')
                if r:r[1]=Atom('yes')
                else:p.append([Atom('remove_unused_layers'),Atom('yes')])
                k=child(p,'keep_end_layers')
                if k:k[1]=Atom('no')
                else:p.append([Atom('keep_end_layers'),Atom('no')])
            for m in kids(s,'model'):s.remove(m)
            for kind in ['fp_line','fp_rect']:
                for g in kids(s,kind):
                    if child(g,'layer')[1] in ['F.CrtYd','F.SilkS']:s.remove(g)
            s.append(parse('(fp_rect (start -1.15 -1.15) (end 3.69 11.31) (stroke (width .05) (type default)) (fill none) (layer "F.CrtYd"))'))
            s.append(parse('(fp_line (start -1.3 -1.3) (end -.2 -1.3) (stroke (width .12) (type default)) (layer "F.SilkS"))'))
            s.append(parse('(fp_line (start -1.3 -1.3) (end -1.3 -.2) (stroke (width .12) (type default)) (layer "F.SilkS"))'))
            # Stacked same-number SMD copper land keeps the reserve FRONT annulus;
            # the coincident PTH pad supplies the drilled/plated 1mm barrel.
            s.append(parse('(pad "10" smd circle (at 2.54 10.16) (size 1.7 1.7) (layers "F.Cu" "F.Mask"))'))
            child(s,'descr')[1]+='; top connected lands only; no populated header; underside header optional'
        else:
            for m in kids(s,'model'):
                rel=re.sub(r'^\$\{[^}]+\}/','',m[1]);srcpath=KICAD/'3dmodels'/rel;dst=ROOT/'libraries/models'/srcpath.name
                shutil.copy2(srcpath,dst);m[1]='${KIPRJMOD}/libraries/models/'+srcpath.name
        (fps/f'{name}.kicad_mod').write_text(dump(s),encoding='utf-8')
    # Drawing: Guangdong Jingmilong TS-1187A low profile variant. A/B top, C/D bottom.
    pads=' '.join(f'(pad "{num}" smd rect (at {x} {y}) (size 1 1) (layers "F.Cu" "F.Paste" "F.Mask"))' for num,x,y in [(1,-3,-1.85),(1,3,-1.85),(2,-3,1.85),(2,3,1.85)])
    def rect(a,b,layer,w=.1):return f'(fp_rect (start {a[0]} {a[1]}) (end {b[0]} {b[1]}) (stroke (width {w}) (type default)) (fill none) (layer "{layer}"))'
    sw=f'''(footprint "TS1187A_4x4x0p8_REF" (version 20241229) (generator "pcbnew") (layer "F.Cu")
    (descr "TS-1187A low profile 4x4x0.8 trade size; actual body4.8, leads6.4; manufacturer land pattern centres6.0x3.7, pads1.0x1.0. User kit match UNVERIFIED. Pin1=physical A/B,2=C/D.")
    (tags "TS-1187A low profile kit verify") (attr smd)
    (fp_text reference "REF**" (at 0 -3.3) (layer "F.SilkS") (effects (font (size .8 .8) (thickness .12))))
    (fp_text value "TS-1187A_0.8" (at 0 3.3) (layer "F.Fab") (effects (font (size .8 .8) (thickness .1))))
    {rect((-2.4,-2.4),(2.4,2.4),'F.Fab')}{rect((-3.75,-2.65),(3.75,2.65),'F.CrtYd',.05)}
    (fp_line (start -2.25 -2.55) (end 2.25 -2.55) (stroke (width .12) (type default)) (layer "F.SilkS"))
    (fp_line (start -2.25 2.55) (end 2.25 2.55) (stroke (width .12) (type default)) (layer "F.SilkS"))
    (fp_circle (center 0 0) (end 1.9 0) (stroke (width .1) (type default)) (fill none) (layer "F.Fab"))
    {pads}
    (model "${{KIPRJMOD}}/libraries/models/TS1187A_envelope.wrl" (offset (xyz 0 0 0)) (scale (xyz 1 1 1)) (rotate (xyz 0 0 0))))'''
    (fps/'TS1187A_4x4x0p8_REF.kicad_mod').write_text(sw,encoding='utf-8')
    (fps/'TouchWire_2x1p5.kicad_mod').write_text('''(footprint "TouchWire_2x1p5" (version 20241229) (generator "pcbnew") (layer "F.Cu") (descr "Electrode wire solder land, no electrode") (attr smd exclude_from_pos_files)
    (fp_text reference "E" (at 0 -1.3) (layer "F.SilkS") (effects (font (size .8 .8) (thickness .12))))
    (fp_text value "TOUCH" (at 0 1.3) (layer "F.Fab") (effects (font (size .8 .8) (thickness .1))))
    (fp_rect (start -.95 -.7) (end .95 .7) (stroke (width .05) (type default)) (fill none) (layer "F.CrtYd"))
    (pad "1" smd roundrect (at 0 0) (size 1.5 1) (layers "F.Cu" "F.Mask") (roundrect_rratio .2)))''',encoding='utf-8')
    (fps/'Mount_M2_Keepout5.kicad_mod').write_text('''(footprint "Mount_M2_Keepout5" (version 20241229) (generator "pcbnew") (layer "F.Cu") (descr "M2 2.2mm NPTH,5mm head envelope") (attr exclude_from_pos_files exclude_from_bom)
    (fp_text reference "H" (at 0 -3) (layer "F.SilkS") (effects (font (size .8 .8) (thickness .12))))
    (fp_text value "M2" (at 0 3) (layer "F.Fab") (effects (font (size .8 .8) (thickness .1))))
    (fp_circle (center 0 0) (end 2.5 0) (stroke (width .05) (type default)) (fill none) (layer "F.CrtYd"))
    (fp_circle (center 0 0) (end 2.3 0) (stroke (width .12) (type default)) (fill none) (layer "F.SilkS"))
    (pad "" np_thru_hole circle (at 0 0) (size 2.2 2.2) (drill 2.2) (layers "*.Cu" "*.Mask")))''',encoding='utf-8')
    # Simplified mechanical envelope: model dimensions in mm, not a manufacturer CAD model.
    def box(x,y,z,sx,sy,sz,color):return f'Transform {{ translation {x} {y} {z} children [ Shape {{ appearance Appearance {{ material Material {{ diffuseColor {color} }} }} geometry Box {{ size {sx} {sy} {sz} }} }} ] }}'
    model='#VRML V2.0 utf8\n'+box(0,0,.2,4.8,4.8,.4,'.13 .14 .16')+'\n'+box(0,0,.43,4.4,4.4,.12,'.65 .67 .7')
    for x in [-2.85,2.85]:
        for y in [-1.85,1.85]:model+='\n'+box(x,y,.08,.7,.5,.16,'.72 .74 .76')
    model+='\nTransform { translation 0 0 .64 rotation 1 0 0 1.570796 children [ Shape { appearance Appearance { material Material { diffuseColor .86 .64 .12 } } geometry Cylinder { radius 1.9 height .32 } } ] }'
    # Single IndexedFaceSet with baked mm/2.54 vertices, for portable KiCad VRML parsing.
    vv=[];ff=[];cc=[]
    def mesh_box(x,y,z,sx,sy,sz,color):
        k=len(vv);vv.extend([(x+dx*sx/2,y+dy*sy/2,z+dz*sz/2) for dx,dy,dz in [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]])
        for f in [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)]:ff.append(tuple(k+i for i in f));cc.append(color)
    mesh_box(0,0,.2,4.8,4.8,.4,0);mesh_box(0,0,.44,4.4,4.4,.08,1)
    for x in [-2.85,2.85]:
        for y in [-1.85,1.85]:mesh_box(x,y,.08,.7,.5,.16,1)
    k=len(vv);nn=48
    for z,r in [(.48,1.9),(.7,1.9),(.8,1.55)]:
        vv.extend([(r*math.cos(i*2*math.pi/nn),r*math.sin(i*2*math.pi/nn),z) for i in range(nn)])
    ff.append(tuple(k+i for i in reversed(range(nn))));cc.append(2)
    ff.append(tuple(k+2*nn+i for i in range(nn)));cc.append(2)
    for row in range(2):
        for i in range(nn):ff.append((k+row*nn+i,k+row*nn+(i+1)%nn,k+(row+1)*nn+(i+1)%nn,k+(row+1)*nn+i));cc.append(2)
    geom='#VRML V2.0 utf8\nShape { appearance Appearance { material Material { diffuseColor 1 1 1 } } geometry IndexedFaceSet { coord Coordinate { point ['+','.join(' '.join(f'{x/2.54:.8f}' for x in v) for v in vv)+'] } coordIndex ['+','.join(' '.join(str(i) for i in f)+' -1' for f in ff)+'] colorPerVertex FALSE color Color { color [.13 .14 .16, .65 .67 .70, .86 .64 .12] } colorIndex ['+','.join(map(str,cc))+'] solid TRUE ccw TRUE } }'
    (ROOT/'libraries/models/TS1187A_envelope.wrl').write_text(geom)
    (ROOT/'fp-lib-table').write_text('(fp_lib_table (version 7) (lib (name "ClickWheel") (type "KiCad") (uri "${KIPRJMOD}/libraries/ClickWheel.pretty") (options "") (descr "Portable project footprints")))')
    (ROOT/'sym-lib-table').write_text('(sym_lib_table (version 7) '+' '.join(f'(lib (name "{lib}") (type "KiCad") (uri "${{KIPRJMOD}}/libraries/{lib}.kicad_sym") (options "") (descr "Portable subset"))' for lib in sorted(set(k.split(':')[0] for k in SYMS)))+')')

ROOTUUID=uid('root');items=[];expected={};pinpos={}
def wire(a,b):items.append(f'(wire (pts (xy {a[0]} {a[1]}) (xy {b[0]} {b[1]})) (stroke (width 0) (type default)) (uuid {q(uid("w"+str(a)+str(b)))}))')
def label(n,x,y,angle=0):items.append(f'(label {q(n)} (at {x} {y} {angle}) (effects (font (size 1.05 1.05)) (justify left bottom)) (uuid {q(uid("l"+n+str(x)+str(y)))}))')
def note(t,x,y,size=1.2):items.append(f'(text {q(t)} (at {x} {y} 0) (effects (font (size {size} {size})) (justify left)) (uuid {q(uid("text"+t))}))')
def place(ref,x,y,angle=0,flag=None,autowire=True):
    c=COMP.get(ref,dict(key='power:PWR_FLAG',value='PWR_FLAG',fp='',nets={'1':flag},uuid=uid(ref)))
    s=SYMS[c['key']];vertical=c['key'].startswith('Device:') and angle==0
    props=[]
    for k,v,px,py,hide in [('Reference',ref,x+3 if vertical else x,y-2 if vertical else y-4,False),('Value',c['value'],x+3 if vertical else x,y if vertical else y-6.5,False),('Footprint',c['fp'],x,y,True),('Datasheet','https://www.best-digi.com/news-74286.html' if ref.startswith('SW') else '',x,y,True)]:
        if ref=='J1' and k in ['Reference','Value']:px=x+5.08;py=y-(11.43 if k=='Reference' else 15.24)
        props.append(f'(property {q(k)} {q(v)} (at {px} {py} 0) (effects (font (size 1.05 1.05)) {"(justify left)" if vertical else ""} {"(hide yes)" if hide else ""}))')
    pinst=[];a=math.radians(angle)
    for sub in kids(s,'symbol'):
        for pin in kids(sub,'pin'):
            num=child(pin,'number')[1];at=child(pin,'at');lx=float(at[1]);ly=-float(at[2]);pa=(float(at[3])+angle)%360
            px=round(x+lx*math.cos(a)+ly*math.sin(a),6);py=round(y-lx*math.sin(a)+ly*math.cos(a),6);pinpos[ref,num]=(px,py)
            pinst.append(f'(pin {q(num)} (uuid {q(uid(ref+"pin"+num))}))');net=c['nets'].get(num)
            if net is None:items.append(f'(no_connect (at {px} {py}) (uuid {q(uid(ref+"NC"+num))}))')
            elif autowire:
                stub=7.62 if c['key'].startswith('Connector_Generic:') else 5.08
                dx=-math.cos(math.radians(pa))*stub;dy=math.sin(math.radians(pa))*stub
                end=(round(px+dx,6),round(py+dy,6));wire((px,py),end);label(net,*end,180 if dx>0 else 0)
            if ref in COMP and net is not None:expected.setdefault(net,[]).append([ref,num])
    items.append(f'(symbol (lib_id {q(c["key"])}) (at {x} {y} {angle}) (unit 1) (in_bom {"no" if ref.startswith(("#","H")) else "yes"}) (on_board {"no" if ref.startswith("#") else "yes"}) (dnp no) (uuid {q(c["uuid"])}) {" ".join(props)} {" ".join(pinst)} (instances (project {q(NAME)} (path {q("/"+ROOTUUID)} (reference {q(ref)}) (unit 1)))))')
def make_schematic():
    note('MonoPod Click Wheel | R1 | ESP32-S3 / firmware v0.8.6',15,16,2.5)
    note('ONE COPPER SIDE: F.Cu | 44 mm round FR4, 1.6 mm | user kit TS-1187A 0.8 mm',15,24,1.3)
    # Labelled terminals make the five parallel branches explicit without wire crossings.
    note('ADC PULL-UP AND FILTER',18,35,1.5)
    place('R1',45.72,53.34);place('C1',83.82,53.34)
    place('#PWR1',24.13,46.99,flag='3V3');place('#PWR2',24.13,64.77,flag='GND')
    note('R1=10k to 3V3, C1=100n to GND. No second pull-up on main board.',18,75,1.05)
    note('FIVE PARALLEL MOMENTARY BRANCHES (ONE KEY AT A TIME)',18,86,1.5)
    for i in range(1,6):
        y=100.33+(i-1)*16.51
        place('SW'+str(i),45.72,y,autowire=False);place('R'+str(i+1),87.63,y,angle=90,autowire=False)
        a=pinpos['SW'+str(i),'1'];b=pinpos['SW'+str(i),'2'];rr=pinpos['R'+str(i+1),'1'];r2=pinpos['R'+str(i+1),'2']
        wire((a[0]-7.62,a[1]),a);label('BTN_ADC',a[0]-7.62,a[1]);wire(b,rr)
        label(COMP['R'+str(i+1)]['nets']['1'],(b[0]+rr[0])/2,y)
        wire(r2,(r2[0]+7.62,y));label('GND',r2[0]+7.62,y,180)
    note('Switch logical 1 = physical A/B (upper row); logical 2 = C/D (lower row).',18,177,1.05)
    note('Verify kit pin pairing and 6.0 x 3.7 mm pad centres before ordering PCB.',18,183,1.05)
    note('OUTPUT: BARE PLATED HOLES, NO CONNECTOR FITTED',134,35,1.5)
    place('J1',157.48,57.15)
    note('1=3V3  2=GND  3=GPIO9  4=GPIO1  5=GPIO2',133,82,1.05)
    note('6=GPIO5  7=GPIO6  8=GPIO7  9=GPIO8  10=NC',133,88,1.05)
    note('FLEXIBLE WIRES TO EXTERNAL FOIL ELECTRODES',134,101,1.5)
    for i in range(1,7):place('E'+str(i),166.37,111.76+(i-1)*8.89)
    for i in range(1,5):place('H'+str(i),242.57,113.03+(i-1)*16.51)
    note('TOUCH1..6 are logical wheel sectors, not ESP32 touch-channel numbers.',18,195,1.05)
    note('At ESP32: six series 510R resistors close to GPIO, not on this PCB.',133,94,1.05)
    note('No sensor electrodes / no ground plane / no vias / no B.Cu routing.',18,189,1.05)
    sch=f'(kicad_sch (version 20250114) (generator "eeschema") (uuid "{ROOTUUID}") (paper "A4") (title_block (title "MonoPod Click Wheel") (date "2026-10-29") (rev "R1") (company "ESP32-S3 / MonoPod v0.8.6")) (lib_symbols {" ".join(dump(s) for s in SYMS.values())}) {" ".join(items)} (embedded_fonts no))'
    (ROOT/f'{NAME}.kicad_sch').write_text(sch,encoding='utf-8')
    (ROOT/'validation/expected_nets.json').write_text(json.dumps(expected,indent=2))
    (ROOT/'validation/components.json').write_text(json.dumps(COMP,indent=2))
def project():
    d={'meta':{'filename':NAME+'.kicad_pro','version':1},'board':{'design_settings':{'rules':{'min_clearance':.25,'min_track_width':.3,'min_via_diameter':.6,'min_through_hole_diameter':.3,'min_hole_to_hole':.25,'min_copper_edge_clearance':.5,'min_silk_clearance':.15,'min_text_height':.8,'min_text_thickness':.1},'drc_exclusions':[]}},'net_settings':{'classes':[{'name':'Default','clearance':.25,'track_width':.3,'via_diameter':.6,'via_drill':.3,'microvia_diameter':.3,'microvia_drill':.1,'diff_pair_width':.25,'diff_pair_gap':.25,'diff_pair_via_gap':.25}],'meta':{'version':4}},'schematic':{'legacy_lib_dir':'','legacy_lib_list':[]}}
    (ROOT/f'{NAME}.kicad_pro').write_text(json.dumps(d,indent=2))
if __name__=='__main__':
    libraries();project();make_schematic();print(ROOT, len(COMP),'components')
