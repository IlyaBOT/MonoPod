from pathlib import Path
import re, json, copy, uuid, math, shutil

ROOT=Path(__file__).resolve().parents[1]
NAME='TDA1308_HeadphoneAmp'
KICAD=Path(r'C:\Program Files\KiCad\10.0\share\kicad')
WIDTH=45; HEIGHT=25
for d in ['libraries','renders','validation','gerbers','docs','scripts']:
    (ROOT/d).mkdir(parents=True,exist_ok=True)
class Atom(str): pass
def parse(s):
    stack=[]; result=None
    for t in re.findall(r'"(?:\\.|[^"\\])*"|[^\s()]+|[()]',s):
        if t=='(':
            a=[]
            if stack: stack[-1].append(a)
            stack.append(a)
        elif t==')': result=stack.pop()
        else: stack[-1].append(json.loads(t) if t.startswith('"') else Atom(t))
    return result
def dump(a):
    if isinstance(a,list): return '('+' '.join(dump(x) for x in a)+')'
    if isinstance(a,Atom): return str(a)
    return json.dumps(a,ensure_ascii=False)
def child(a,k): return next((x for x in a if isinstance(x,list) and x and x[0]==k),None)
def kids(a,k): return [x for x in a if isinstance(x,list) and x and x[0]==k]
def uid(s): return str(uuid.uuid5(uuid.NAMESPACE_URL,'monopod-tda1308-r1/'+s))
def q(s): return json.dumps(str(s),ensure_ascii=False)
SYMS={}
def addlib(lib,name):
    key=lib+':'+name
    syms={s[1]:s for s in kids(parse((KICAD/'symbols'/f'{lib}.kicad_sym').read_text(encoding='utf-8')),'symbol')}
    def flat(n):
        s=copy.deepcopy(syms[n]); ex=child(s,'extends')
        if ex:
            parent=flat(ex[1]); s.remove(ex); own={x[1] for x in kids(s,'property')}
            s+=copy.deepcopy([x for x in parent[2:] if not (isinstance(x,list) and x[0]=='property' and x[1] in own)])
            for sub in kids(s,'symbol'): sub[1]=str(sub[1]).replace(ex[1]+'_',n+'_')
        return s
    s=flat(name);s[1]=key;SYMS[key]=s
    return key
for lib,n in [('Amplifier_Audio','TDA1308'),('Device','R'),('Device','C'),('Device','C_Polarized'),('Connector_Generic','Conn_01x01'),('Connector','TestPoint'),('power','PWR_FLAG')]:addlib(lib,n)
COMP={}; RFP='Resistor_SMD:R_0805_2012Metric'; CFP='Capacitor_SMD:C_1206_3216Metric'
def comp(ref,key,value,fp,nets,x,y,rot=0,dnp=False,role=''):
    COMP[ref]=dict(ref=ref,key=key,value=value,fp=fp,nets={str(k):v for k,v in nets.items()},x=x,y=y,rot=rot,dnp=dnp,uuid=uid(ref),role=role)
def res(n,v,a,b,x,y,rot=0,dnp=False,role=''):
    comp('R'+str(n),'Device:R',v,RFP,{1:a,2:b},x,y,rot,dnp,role)
comp('U1','Amplifier_Audio:TDA1308','TDA1308T','Package_SO:SOIC-8_3.9x4.9mm_P1.27mm',{1:'OUTA_RAW',2:'INV_A',3:'VREF',4:'GND',5:'VREF',6:'INV_B',7:'OUTB_RAW',8:'VDD'},25,16,role='SO8, salvaged chip marked TDA1308; functionality unverified')
res(1,'10k','VDD','VREF',17.8,8.0,role='VREF upper divider, 1%')
res(2,'10k','VREF','GND',17.8,10.5,role='VREF lower divider, 1%')
res(3,'10k','ATT_L_IN','ATT_L',9.2,10.5,role='Left attenuation series, 1%')
res(4,'3.3k','ATT_L','GND',12.7,10.5,90,role='Left attenuation shunt, 1%')
res(5,'10k','ATT_R_IN','ATT_R',9.2,19,role='Right attenuation series, 1%')
res(6,'3.3k','ATT_R','GND',12.7,19,90,role='Right attenuation shunt, 1%')
res(7,'3.9k','AC_A','INV_A',17.3,18,role='Left input, 1%')
res(8,'3.9k','AC_B','INV_B',29.5,20.2,90,role='Right input, 1%')
res(9,'3.9k','OUTA_RAW','INV_A',19.9,14.7,-90,role='Left feedback, 1%')
res(10,'3.9k','OUTB_RAW','INV_B',30,16,-90,role='Right feedback, 1%')
res(11,'2.2R','OUTA_RAW','OUTA_C',21,10.5,90,role='Left output isolation, 0R permitted after bench test')
res(12,'2.2R','OUTB_RAW','OUTB_C',37.5,11.1,90,role='Right output isolation, 0R permitted after bench test')
res(13,'100k','HP_L','GND',38.5,15,role='Left output bleed')
res(14,'100k','HP_R','GND',38.5,19,role='Right output bleed')
res(15,'0R','LIN','ATT_L_IN',5.5,10.5,role='ATT L input link, FIT default')
res(16,'0R','ATT_L','INA',9.2,13,role='ATT L output link, FIT default')
res(17,'0R','RIN','ATT_R_IN',5.5,19,role='ATT R input link, FIT default')
res(18,'0R','ATT_R','INB',9.2,21.5,role='ATT R output link, FIT default')
res(19,'0R','LIN','INA',5.5,13,dnp=True,role='DIRECT L, DNP default; remove R15/R16 before fitting')
res(20,'0R','RIN','INB',5.5,21.5,dnp=True,role='DIRECT R, DNP default; remove R17/R18 before fitting')
def cap(n,v,fp,a,b,x,y,rot=0,polar=False,role=''):
    comp('C'+str(n),'Device:C_Polarized' if polar else 'Device:C',v,fp,{1:a,2:b},x,y,rot,role=role)
cap(1,'22u / >=10V','Capacitor_THT:CP_Radial_D5.0mm_P2.00mm','VREF','GND',13.5,4.5,polar=True,role='VREF, plus pin1')
cap(2,'100u / >=10V','Capacitor_THT:CP_Radial_D6.3mm_P2.50mm','VDD','GND',6,4.8,polar=True,role='Supply bulk, plus pin1; 100u 25V possible if size fits')
cap(3,'100n / 25V X7R','Capacitor_SMD:C_0805_2012Metric','VDD','GND',30.6,12.5,role='Local bypass next to U1 pin8; nearby GND via to pin4 return plane')
cap(4,'4.7u / 25V X7R',CFP,'INA','AC_A',13.6,15.6,role='Nonpolar input; effective capacitance >=2.2u at bias')
cap(5,'4.7u / 25V X7R',CFP,'INB','AC_B',23.5,21,role='Nonpolar input; effective capacitance >=2.2u at bias')
cap(6,'220u / >=10V','Capacitor_THT:CP_Radial_D8.0mm_P3.50mm','OUTA_C','HP_L',22.25,5,polar=True,role='Left output, plus pin1 toward IC; 470u or 100u25V alternatives')
cap(7,'220u / >=10V','Capacitor_THT:CP_Radial_D8.0mm_P3.50mm','OUTB_C','HP_R',31.25,5,polar=True,role='Right output, plus pin1 toward IC; 470u or 100u25V alternatives')
WFP='Connector_Wire:SolderWire-0.5sqmm_1x01_D0.9mm_OD2.1mm'
for i,(net,x,y) in enumerate([('VDD',1.8,4),('GND',1.8,9),('LIN',1.8,14),('RIN',1.8,19),('GND',43,11.5),('HP_L',43,15.5),('HP_R',43,20.5)],1):
    comp('J'+str(i),'Connector_Generic:Conn_01x01', ['VCC','GND','LIN','RIN','GND','LOUT','ROUT'][i-1],WFP,{1:net},x,y,role='Solder wire AWG26-30; plated drill 1.15mm')
for i,(net,x,y) in enumerate([('VDD',18.2,1.6),('GND',1.8,23),('VREF',15.5,21.8),('INA',15.8,13),('INB',19,23.5),('OUTA_RAW',19.3,20.6),('OUTB_RAW',33,16),('HP_L',34,12),('HP_R',35,21.5)],1):
    comp('TP'+str(i),'Connector:TestPoint',net,'TestPoint:TestPoint_Pad_D1.5mm',{1:net},x,y,role='Exposed 1.5mm probe pad')

ROOTUUID=uid('root');items=[];expected={};pinpos={}
def pins(s,unit):
    return [pin for sub in kids(s,'symbol') if int(str(sub[1]).split('_')[-2]) in (0,unit) for pin in kids(sub,'pin')]
def wire(a,b):
    items.append(f'(wire (pts (xy {a[0]} {a[1]}) (xy {b[0]} {b[1]})) (stroke (width 0.254) (type default)) (uuid {q(uid("wire"+str(a)+str(b)))}))')
def label(net,x,y,ang=0):
    items.append(f'(label {q(net)} (at {x} {y} {ang}) (effects (font (size 1.05 1.05)) (justify left bottom)) (uuid {q(uid("label"+net+str(x)+str(y)))}))')
def note(t,x,y,size=1.3):
    items.append(f'(text {q(t)} (at {x} {y} 0) (effects (font (size {size} {size})) (justify left)) (uuid {q(uid("note"+t))}))')
def place(ref,x,y,unit=1,angle=0,flagnet=None,autowire=True):
    x=round(round(x/1.27)*1.27,6);y=round(round(y/1.27)*1.27,6)
    c=COMP.get(ref,dict(key='power:PWR_FLAG',value='PWR_FLAG',fp='',nets={'1':flagnet},uuid=uid(ref),dnp=False,role=''))
    s=SYMS[c['key']];inst=c['uuid'] if unit==1 else uid(ref+'/unit'+str(unit));pns=pins(s,unit)
    vertical=(c['key'].startswith('Device:') and angle==0)
    props=[]
    for k,v,px,py,hidden in [('Reference',ref,x+3 if vertical else x,y-2 if vertical else y-5,False),('Value',c['value'],x+3 if vertical else x,y if vertical else y-7.5,False),('Footprint',c['fp'],x,y,True),('Datasheet',next((p[2] for p in kids(s,'property') if p[1]=='Datasheet'),''),x,y,True)]:
        props.append(f'(property {q(k)} {q(v)} (at {px} {py} {angle}) (effects (font (size 1.05 1.05)) {"(justify left)" if vertical else ""} {"(hide yes)" if hidden else ""}))')
    a=math.radians(angle);pinst=[]
    for p in pns:
        num=child(p,'number')[1];pinst.append(f'(pin {q(num)} (uuid {q(uid(inst+"pin"+num))}))')
        at=child(p,'at');lx=float(at[1]);ly=-float(at[2]);pa=(int(at[3])+angle)%360
        px=round(x+lx*math.cos(a)+ly*math.sin(a),6);py=round(y-lx*math.sin(a)+ly*math.cos(a),6);pinpos[ref,num]=(px,py)
        net=c['nets'][num]
        if autowire:
            dx=-math.cos(math.radians(pa))*5.08;dy=math.sin(math.radians(pa))*5.08
            end=(round(px+dx,6),round(py+dy,6));wire((px,py),end);label(net,*end,180 if dx>0 else 0)
        if ref in COMP:expected.setdefault(net,[]).append([ref,num])
    items.append(f'(symbol (lib_id {q(c["key"])}) (at {x} {y} {angle}) (unit {unit}) (in_bom {"no" if ref.startswith("#") else "yes"}) (on_board {"no" if ref.startswith("#") else "yes"}) (dnp {"yes" if c["dnp"] else "no"}) (uuid {q(inst)}) {" ".join(props)} {" ".join(pinst)} (instances (project {q(NAME)} (path {q("/"+ROOTUUID)} (reference {q(ref)}) (unit {unit})))))')
def stub(ref,num,dx,dy):
    pt=pinpos[ref,str(num)];end=(round(pt[0]+dx,6),round(pt[1]+dy,6));wire(pt,end);label(COMP[ref]['nets'][str(num)],*end,180 if dx>0 else 0)
def junction(pt):items.append(f'(junction (at {pt[0]} {pt[1]}) (diameter 0.8) (color 0 0 0 0) (uuid {q(uid("j"+str(pt)))}))')
def make_schematic():
    note('MonoPod | TDA1308 stereo headphone amplifier',15,17,3)
    note('R1 | 3.3 V default, 5 V only after bench validation | 32 ohm load | 45 x 25 mm PCB',15,25,1.4)
    note('SUPPLY / HALF-SUPPLY REFERENCE',15,34,1.8)
    for ref,x,y,a in [('R1',65,50,0),('R2',65,72,0),('C1',110,72,0),('C2',155,60,0),('C3',207,60,0)]:place(ref,x,y,angle=a)
    place('U1',252.73,60.96,unit=3)
    place('#FLG1',22.86,46.99,flagnet='VDD');place('#FLG2',22.86,71.12,flagnet='GND')
    note('C1/C2: + to VREF/VDD. C3: next to U1 pin 8, short GND via.',15,89,1.25)
    note('LEFT CHANNEL / ATTENUATED DEFAULT',15,102,1.8)
    note('RIGHT CHANNEL / ATTENUATED DEFAULT',15,177,1.8)
    for right,y in [(False,128.27),(True,203.2)]:
        # Signal path rendered with explicit interconnections; channel labels join selectors.
        ch='B' if right else 'A';u=2 if right else 1;cin='C5' if right else 'C4';rin='R8' if right else 'R7';rf='R10' if right else 'R9';ro='R12' if right else 'R11';co='C7' if right else 'C6';bleed='R14' if right else 'R13'
        place(cin,114.3,y+2.54,angle=90,autowire=False)
        place(rin,139.7,y+2.54,angle=90,autowire=False)
        place('U1',172.72,y,unit=u,autowire=False)
        place(rf,172.72,y-16.51,angle=270,autowire=False)
        # U1 minus input lies 2.54 below center; plus input above.
        wire(pinpos[cin,'2'],pinpos[rin,'1']);wire(pinpos[rin,'2'],pinpos['U1','6' if right else '2'])
        label('AC_'+ch,*pinpos[cin,'2'])
        minus=pinpos['U1','6' if right else '2'];out=pinpos['U1','7' if right else '1'];plus=pinpos['U1','5' if right else '3']
        corner=(156.21,minus[1]);wire(corner,(corner[0],y-16.51));wire((corner[0],y-16.51),pinpos[rf,'2'])
        wire(pinpos[rf,'1'],(194.31,y-16.51));wire((194.31,y-16.51),(194.31,y));wire(out,(194.31,y));junction(corner);junction((194.31,y))
        label('INV_'+ch,*minus);label('OUT'+ch+'_RAW',194.31,y)
        stub('U1','5' if right else '3',-7.62,0)
        place(ro,213.36,y,angle=90,autowire=False);wire((194.31,y),pinpos[ro,'1'])
        place(co,238.76,y,angle=90,autowire=False)
        wire(pinpos[ro,'2'],pinpos[co,'1'])
        label('OUT'+ch+'_C',*pinpos[ro,'2'])
        stub(cin,1,-7.62,0)
        stub(co,2,10.16,0)
        place(bleed,269.24,y+17.78)
        # Attenuator/link matrices are labelled explicitly for reconfiguration.
        for r,x,yy in [(('R17' if right else 'R15'),30.48,y-8.89),(('R5' if right else 'R3'),71.12,y-8.89),(('R6' if right else 'R4'),71.12,y+11.43),(('R18' if right else 'R16'),30.48,y+13.97),(('R20' if right else 'R19'),30.48,y+34.29)]:place(r,x,yy,angle=90 if r not in ['R4','R6'] else 0)
    note('MODE LINKS: ATT fit R15/R16/R17/R18; leave R19/R20 DNP.',15,253,1.25)
    note('DIRECT L: remove R15 AND R16, fit R19. DIRECT R: remove R17 AND R18, fit R20.',15,260,1.25)
    note('Two ATT links disconnect both ends of each divider in DIRECT. Never fit ATT and DIRECT together.',15,267,1.25)
    note('Loaded ATT midband = -16.4 dB before 2.2R output. Active stage = -1. Input capacitors nonpolar.',15,274,1.2)
    note('C6/C7 positive terminals toward U1. No headphones on RAW outputs. Cable stability requires bench test.',15,281,1.2)
    note('WIRE PADS',304,34,1.8)
    for i in range(1,8):place('J'+str(i),323.85,48.26+(i-1)*16.51)
    note('TEST PADS',363,34,1.8)
    for i in range(1,10):place('TP'+str(i),382.27,48.26+(i-1)*20.32)
    note('INA/INB: selected signal BEFORE C4/C5.',292,222,1.15)
    note('2 layers, FR4 1.6 mm; all components on top.',292,229,1.15)
    note('U1 pin1 = OUTA; SO8 1.27 mm, not TSSOP.',292,236,1.15)
    note('NXP TDA1308 Rev.5 (2011), Fig.4 p.6.',292,243,1.15)
    sch=f'(kicad_sch (version 20250114) (generator "eeschema") (uuid "{ROOTUUID}") (paper "A3") (title_block (title "MonoPod TDA1308 Headphone Amplifier") (date "2026-10-27") (rev "R1") (company "3.3V / 32ohm | bench qualification required")) (lib_symbols {" ".join(dump(s) for s in SYMS.values())}) {" ".join(items)} (embedded_fonts no))'
    (ROOT/f'{NAME}.kicad_sch').write_text(sch,encoding='utf-8')
    (ROOT/'validation'/'expected_nets.json').write_text(json.dumps(expected,indent=2),encoding='utf-8')
    (ROOT/'validation'/'components.json').write_text(json.dumps(COMP,indent=2),encoding='utf-8')
def libraries():
    for lib in set(k.split(':')[0] for k in SYMS):shutil.copy2(KICAD/'symbols'/f'{lib}.kicad_sym',ROOT/'libraries'/f'{lib}.kicad_sym')
    libs=set()
    for c in COMP.values():
        lib,n=c['fp'].split(':');libs.add(lib);dest=ROOT/'libraries'/f'{lib}.pretty';dest.mkdir(exist_ok=True)
        content=(KICAD/'footprints'/f'{lib}.pretty'/f'{n}.kicad_mod').read_text(encoding='utf-8')
        for rel in re.findall(r'\$\{KICAD\d+_3DMODEL_DIR\}/([^"\n]+)',content):
            src=KICAD/'3dmodels'/rel;dst=ROOT/'libraries'/'models'/rel
            if not src.exists():
                if lib!='Connector_Wire':raise RuntimeError('Missing 3D model '+str(src))
                parsed=parse(content)
                for model in list(kids(parsed,'model')):parsed.remove(model)
                content=dump(parsed)
                continue
            dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
        content=re.sub(r'\$\{KICAD\d+_3DMODEL_DIR\}','${KIPRJMOD}/libraries/models',content)
        (dest/f'{n}.kicad_mod').write_text(content,encoding='utf-8')
    (ROOT/'fp-lib-table').write_text('(fp_lib_table (version 7) '+ ' '.join(f'(lib (name "{l}") (type "KiCad") (uri "${{KIPRJMOD}}/libraries/{l}.pretty") (options "") (descr "Bundled KiCad 10.0.6 footprint"))' for l in sorted(libs))+')',encoding='utf-8')
    (ROOT/'sym-lib-table').write_text('(sym_lib_table (version 7) '+ ' '.join(f'(lib (name "{l}") (type "KiCad") (uri "${{KIPRJMOD}}/libraries/{l}.kicad_sym") (options "") (descr "KiCad 10.0.6"))' for l in sorted(set(k.split(':')[0] for k in SYMS)))+')',encoding='utf-8')
def project():
    d={'meta':{'filename':NAME+'.kicad_pro','version':1},'board':{'design_settings':{'rules':{'min_clearance':0.2,'min_track_width':0.25,'min_via_diameter':0.65,'min_through_hole_diameter':0.3,'min_hole_to_hole':0.25,'min_copper_edge_clearance':0.5,'min_silk_clearance':0.15,'min_text_height':0.7,'min_text_thickness':0.1},'drc_exclusions':[]}},'net_settings':{'classes':[{'name':'Default','clearance':0.2,'track_width':0.25,'via_diameter':0.65,'via_drill':0.3,'microvia_diameter':0.3,'microvia_drill':0.1,'diff_pair_width':0.25,'diff_pair_gap':0.25,'diff_pair_via_gap':0.25}],'meta':{'version':4}},'schematic':{'legacy_lib_dir':'','legacy_lib_list':[]}}
    (ROOT/f'{NAME}.kicad_pro').write_text(json.dumps(d,indent=2),encoding='utf-8')
if __name__=='__main__':
    libraries();project();make_schematic();print('Generated',len(COMP),'components')
