import os,sys,math,heapq,time,json
import xml.etree.ElementTree as ET
import numpy as np
from PIL import Image,ImageDraw
import pcbnew as p
from design import ROOT,NAME,KICAD,COMP,ROOTUUID,WIDTH,HEIGHT
os.environ['KIPRJMOD']=str(ROOT)
project_path=ROOT/f'{NAME}.kicad_pro'
engineering_rules=json.loads(project_path.read_text())['board']['design_settings']['rules'].copy()
board=p.BOARD();board.SetCopperLayerCount(2)
board.GetDesignSettings().SetBoardThickness(p.FromMM(1.6))
board.GetDesignSettings().m_SolderMaskExpansion=p.FromMM(.05)
board.GetDesignSettings().m_SolderMaskMinWidth=p.FromMM(.1)
vec=lambda x,y:p.VECTOR2I(p.FromMM(float(x)),p.FromMM(float(y)))
layers=[p.F_Cu,p.B_Cu];nets={};padnets={}
xml=ET.parse(ROOT/'validation'/'netlist.xml').getroot()
expected=json.loads((ROOT/'validation'/'expected_nets.json').read_text())
actual={}
for xn in xml.find('nets'):
    name=xn.attrib['name'];short=name.lstrip('/')
    n=p.NETINFO_ITEM(board,name,int(xn.attrib['code']));board.Add(n);nets[short]=n
    actual[short]=[]
    for node in xn.findall('node'):
        ref=node.attrib['ref'];num=node.attrib['pin'];padnets[ref,num]=n;actual[short].append([ref,num])
assert {k:sorted(v) for k,v in actual.items()}=={k:sorted(v) for k,v in expected.items()},'Expected/netlist discrepancy'
(ROOT/'validation'/'netlist_check.txt').write_text('PASS: all schematic net members match independently specified component pin/net map.\n',encoding='utf-8')
fps={}
for ref,c in COMP.items():
    lib,n=c['fp'].split(':');fp=p.FootprintLoad(str(ROOT/'libraries'/f'{lib}.pretty'),n)
    assert fp is not None,ref
    fp.SetReference(ref);fp.SetValue(c['value']);fp.SetFPID(p.LIB_ID(lib,n));fp.SetPath(p.KIID_PATH('/'+ROOTUUID+'/'+c['uuid']))
    board.Add(fp);fp.SetPosition(vec(c['x'],c['y']));fp.SetOrientationDegrees(c['rot']);fp.SetDNP(c['dnp']);fp.SetExcludedFromBOM(False);fp.SetExcludedFromPosFiles(False)
    if fp.GetField(p.FIELD_T_DATASHEET):
        fp.GetField(p.FIELD_T_DATASHEET).SetText('https://www.nxp.com/docs/en/data-sheet/TDA1308.pdf' if ref=='U1' else '')
    fp.Value().SetVisible(False);fp.Reference().SetTextSize(vec(.8,.8));fp.Reference().SetTextThickness(p.FromMM(.12));fp.Reference().SetTextAngle(p.EDA_ANGLE(0,p.DEGREES_T))
    for pad in fp.Pads():pad.SetNet(padnets[ref,pad.GetNumber()])
    for g in fp.GraphicalItems():
        if isinstance(g,p.PCB_TEXT) and g.GetText()=='${REFERENCE}':g.SetVisible(False)
    fps[ref]=fp
def text(s,x,y,size=.75,layer=p.F_SilkS,angle=0):
    size=max(.8,size)
    t=p.PCB_TEXT(board);t.SetText(s);t.SetPosition(vec(x,y));t.SetLayer(layer);t.SetTextSize(vec(size,size));t.SetTextThickness(p.FromMM(.12));t.SetTextAngle(p.EDA_ANGLE(angle,p.DEGREES_T));t.SetMirrored(layer in [p.B_SilkS,p.B_Fab]);board.Add(t);return t
for a,b in [((0,0),(WIDTH,0)),((WIDTH,0),(WIDTH,HEIGHT)),((WIDTH,HEIGHT),(0,HEIGHT)),((0,HEIGHT),(0,0))]:
    s=p.PCB_SHAPE();s.SetShape(p.SHAPE_T_SEGMENT);s.SetStart(vec(*a));s.SetEnd(vec(*b));s.SetLayer(p.Edge_Cuts);s.SetWidth(p.FromMM(.05));board.Add(s)
# Routing masks are conservative preliminary checks; native KiCad DRC is authoritative.
exec((__import__('pathlib').Path(__file__).parent/'router_core.py').read_text(encoding='utf-8'))
def route_all():
    start=time.time()
    order=['INV_A','INV_B','OUTA_RAW','OUTB_RAW','AC_A','AC_B','OUTA_C','OUTB_C','VDD','VREF','INA','INB','ATT_L_IN','ATT_L','ATT_R_IN','ATT_R','LIN','RIN','HP_L','HP_R']
    for name in order:
        code=nets[name].GetNetCode()
        for a,b in pairs(pernet[code]):
            try:path=route(a,b,code,0,front_only=True)
            except RuntimeError:path=route(a,b,code,0)
            commit(path,a,b,code)
        print('ROUTED',name,flush=True)
    # Ground vias at the SMD grounds; THT returns enter bottom ground plane directly.
    code=nets['GND'].GetNetCode()
    for d in pernet[code]:
        if 1 in d['ls']:continue
        obs,vobs=masks(code);candidates=[]
        for dy in np.arange(-1.2,1.21,.1):
            for dx in np.arange(-1.2,1.21,.1):
                if dx*dx+dy*dy<.45**2:continue
                x=round(d['x']+dx,1);y=round(d['y']+dy,1);ix=round(x/STEP);iy=round(y/STEP)
                if not(0<=ix<NX and 0<=iy<NY) or vobs[:,iy,ix].any():continue
                candidates.append((dx*dx+dy*dy,x,y))
        success=False
        for _,x,y in sorted(candidates):
            b={'x':x,'y':y,'ls':[0],'ref':'GNDvia','num':'0'}
            try:path=route(d,b,code,0,front_only=True)
            except RuntimeError:continue
            commit(path,d,b,code)
            v=p.PCB_VIA(board);v.SetPosition(vec(x,y));v.SetWidth(p.FromMM(.65));v.SetDrill(p.FromMM(.3));v.SetViaType(p.VIATYPE_THROUGH);v.SetLayerPair(p.F_Cu,p.B_Cu);v.SetNet(nets['GND']);board.Add(v)
            success=True;break
        if not success:raise RuntimeError('No ground via possible '+d['ref'])
    # Remove any duplicate vias and stubs not used for a layer transition.
    seen=set()
    for t in list(board.GetTracks()):
        if not isinstance(t,p.PCB_VIA):continue
        key=(t.GetNetCode(),t.GetPosition().x,t.GetPosition().y)
        if key in seen:board.Remove(t)
        seen.add(key)
    for layer in layers:
        z=p.ZONE(board);z.SetLayer(layer);z.SetNet(nets['GND']);z.SetLocalClearance(p.FromMM(.2));z.SetPadConnection(p.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(p.FromMM(.25));z.SetThermalReliefSpokeWidth(p.FromMM(.3));z.SetMinThickness(p.FromMM(.2));z.SetIslandRemovalMode(p.ISLAND_REMOVAL_MODE_ALWAYS);z.SetMinIslandArea(0)
        z.Outline().NewOutline()
        for x,y in [(.5,.5),(WIDTH-.5,.5),(WIDTH-.5,HEIGHT-.5),(.5,HEIGHT-.5)]:z.Outline().Append(p.FromMM(x),p.FromMM(y))
        board.Add(z)
    print('Route time',round(time.time()-start,1),'s',flush=True)
def silk():
    # Place reference fields in free silkscreen space near each footprint.
    occupied=[]
    def bb(item,margin=0):
        r=item.GetBoundingBox();return (p.ToMM(r.GetLeft())-margin,p.ToMM(r.GetTop())-margin,p.ToMM(r.GetRight())+margin,p.ToMM(r.GetBottom())+margin)
    def overlap(a,b):
        if b[0]=='circle':
            _,x,y,r=b
            dx=x-max(a[0],min(x,a[2]));dy=y-max(a[1],min(y,a[3]))
            return dx*dx+dy*dy<r*r
        return a[0]<b[2] and a[2]>b[0] and a[1]<b[3] and a[3]>b[1]
    # Pad/mask keepouts; body/courtyard area helps keep labels accessible after assembly.
    for fp in fps.values():
        for pad in fp.Pads():occupied.append(bb(pad,.25))
        for g in fp.GraphicalItems():
            if g.GetLayer()==p.F_SilkS and not isinstance(g,p.PCB_TEXT):
                rect=bb(g,.12)
                if g.GetShape()==p.SHAPE_T_CIRCLE:occupied.append(('circle',(rect[0]+rect[2])/2,(rect[1]+rect[3])/2,(rect[2]-rect[0])/2))
                else:occupied.append(rect)
    # Priority terminal names, then component references.
    wanted=[]
    for ref in ['J1','J2','J3','J4','J5','J6','J7']:
        fp=fps[ref];fp.Reference().SetVisible(False)
        wanted.append((text(fp.GetValue(),COMP[ref]['x'],COMP[ref]['y'],.7),ref))
    for ref,fp in sorted(fps.items(),key=lambda kv:(0 if kv[0].startswith('C') else 1 if kv[0].startswith('TP') else 2,kv[0])):
        if ref.startswith('J'):continue
        wanted.append((fp.Reference(),ref))
    preferred={'J1':(1.8,1.3),'J2':(1.8,6.7),'J3':(1.8,11.7),'J4':(1.8,16.7),'J5':(43,9),'J6':(43,13.2),'J7':(43,18.2),
        'C1':(14.5,.75),'C2':(11.2,1.1),'C6':(24,.75),'C7':(33,.75),'C4':(13.6,17.3),'C5':(23.5,23),'TP1':(18.2,3.6),'TP2':(1.8,21.4),'R19':(5.5,14.7),'R20':(5.5,23.2),'R3':(9.2,8.8),'R7':(17.3,16.3)}
    for item,ref in wanted:
        x=COMP[ref]['x'];y=COMP[ref]['y'];candidates=[]
        if ref.startswith('C') and COMP[ref]['key']=='Device:C_Polarized':x+=1.75 if ref in ['C6','C7'] else 1.25 if ref=='C2' else 1
        for dy in np.arange(-7,7.01,.25):
            for dx in np.arange(-7,7.01,.25):
                score=dx*dx+dy*dy
                if abs(dy)<.8:score+=1.5
                candidates.append((score,x+dx,y+dy))
        if ref in preferred:candidates.append((-1,*preferred[ref]))
        found=False
        for _,xx,yy in sorted(candidates):
            item.SetPosition(vec(xx,yy));rect=bb(item,.1)
            if rect[0]<.3 or rect[1]<.3 or rect[2]>WIDTH-.3 or rect[3]>HEIGHT-.3:continue
            if any(overlap(rect,r) for r in occupied):continue
            occupied.append(rect);found=True;break
        if not found:print('NO SILK SPACE',ref,flush=True)
    # This close-fit label was checked with native KiCad DRC (bounding-box packer is conservative).
    manual_refs={'U1':(25,12.1),'C1':(14.5,1),'C2':(7.25,.8),'C3':(30.6,10.8),'C6':(24,10.25),'C7':(33,10.25),
      'R1':(17.8,6.5),'R2':(17.8,12.2),'R3':(9.2,8.8),'R4':(14.4,10.5),'R5':(9.2,17.3),'R6':(14.4,19),'R7':(17.3,16.3),'R8':(29.5,22.8),'R9':(18,14.6),'R10':(32,18.3),'R12':(37.5,8.7),'R13':(38.5,16.6),'R14':(38.5,20.6),'R15':(5.5,8.8),'R16':(9.2,14.6),'R17':(5.5,17.3),'R18':(9.2,23.2),'R19':(5.5,14.6),'R20':(5.5,23.2),
      'TP1':(18.6,3.7),'TP2':(1.8,21.4),'TP3':(15.5,23.7),'TP4':(13.5,13.2),'TP5':(21.3,23.6),'TP6':(16.9,20.2)}
    manual_refs.update({'U1':(26.3,12),'R11':(23.5,12),'C4':(10.5,16.4),'TP7':(35.5,16)})
    for ref,xy in manual_refs.items():fps[ref].Reference().SetPosition(vec(*xy))
    fps['C4'].Reference().SetTextAngle(p.EDA_ANGLE(90,p.DEGREES_T))
    # Readable underside key, with plating/mask clearances verified by DRC.
    text('MonoPod TDA1308 R1',20,9,1,p.B_SilkS)
    text('3.3V / 5V | 45x25mm',20,11,.8,p.B_SilkS)
    text('ATT:15 16 17 18 ON; 19 20 OFF',20,13,.7,p.B_SilkS)
    text('DIRECT L:15 16 OFF,19 ON',20,15,.7,p.B_SilkS)
    text('DIRECT R:17 18 OFF,20 ON',20,17,.7,p.B_SilkS)
    text('TP1 VDD  TP2 GND  TP3 VREF',20,19,.7,p.B_SilkS)
    text('TP4 INA  TP5 INB  TP6 OUTA_RAW',20,21,.7,p.B_SilkS)
    text('TP7 OUTB_RAW  TP8 HP_L  TP9 HP_R',20,23,.7,p.B_SilkS)
if __name__=='__main__':
    route_all();silk();board.BuildConnectivity();p.ZONE_FILLER(board).Fill(board.Zones());p.SaveBoard(str(ROOT/f'{NAME}.kicad_pcb'),board)
    project=json.loads(project_path.read_text());settings=project['board']['design_settings'];settings['rules'].update(engineering_rules);settings.pop('rule_severities',None)
    project_path.write_text(json.dumps(project,indent=2)+'\n',encoding='utf-8')
    print('Saved',len(list(board.GetTracks())),'tracks/vias',flush=True)
