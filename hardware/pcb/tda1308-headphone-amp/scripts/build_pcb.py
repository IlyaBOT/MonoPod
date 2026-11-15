from pathlib import Path
import os,sys,math,json,time,heapq,random
import numpy as np
from PIL import Image,ImageDraw
import xml.etree.ElementTree as ET
import pcbnew as p
from design import ROOT,NAME,KICAD,COMP,ROOTUUID,WIDTH,HEIGHT
os.environ['KIPRJMOD']=str(ROOT)
vec=lambda x,y:p.VECTOR2I(p.FromMM(float(x)),p.FromMM(float(y)))
board=p.BOARD();board.SetCopperLayerCount(2)
board.GetDesignSettings().SetBoardThickness(p.FromMM(1.6))
board.GetDesignSettings().m_SolderMaskExpansion=p.FromMM(.05)
board.GetDesignSettings().m_SolderMaskMinWidth=p.FromMM(.1)
tb=p.TITLE_BLOCK();tb.SetTitle('MonoPod TDA1308 - single copper side');tb.SetRevision('R2');tb.SetDate('2026-10-27');board.SetTitleBlock(tb)
engineering=json.loads((ROOT/f'{NAME}.kicad_pro').read_text())['board']['design_settings']['rules'].copy()
nets={};padnets={};actual={}
for xn in ET.parse(ROOT/'validation/netlist.xml').getroot().find('nets'):
    name=xn.attrib['name'];net=p.NETINFO_ITEM(board,name,int(xn.attrib['code']));board.Add(net);nets[name.lstrip('/')]=net;actual[name.lstrip('/')]=[]
    for n in xn.findall('node'):padnets[n.attrib['ref'],n.attrib['pin']]=net;actual[name.lstrip('/')].append([n.attrib['ref'],n.attrib['pin']])
assert {k:sorted(v) for k,v in actual.items()}=={k:sorted(v) for k,v in json.loads((ROOT/'validation/expected_nets.json').read_text()).items()}
fps={}
for ref,c in COMP.items():
    lib,name=c['fp'].split(':');fp=p.FootprintLoad(str(ROOT/'libraries'/f'{lib}.pretty'),name)
    assert fp is not None
    fp.SetReference(ref);fp.SetValue(c['value']);fp.SetFPID(p.LIB_ID(lib,name));fp.SetPath(p.KIID_PATH('/'+ROOTUUID+'/'+c['uuid']))
    board.Add(fp);fp.SetPosition(vec(c['x'],c['y']))
    if c['side']=='B':fp.Flip(fp.GetPosition(),True)
    fp.SetOrientationDegrees((c['rot']+180)%360 if c['side']=='B' else c['rot']);fp.SetDNP(False);fp.SetExcludedFromBOM(ref.startswith('JP'));fp.SetExcludedFromPosFiles(False)
    if fp.GetField(p.FIELD_T_DATASHEET):fp.GetField(p.FIELD_T_DATASHEET).SetText('https://www.nxp.com/docs/en/data-sheet/TDA1308.pdf' if ref=='U1' else '')
    fp.Value().SetVisible(False);fp.Reference().SetTextSize(vec(.8,.8));fp.Reference().SetTextThickness(p.FromMM(.12));fp.Reference().SetTextAngle(p.EDA_ANGLE(0,p.DEGREES_T));fp.Reference().SetMirrored(c['side']=='B')
    for pad in fp.Pads():
        pad.SetNet(padnets[ref,pad.GetNumber()]);assert pad.IsOnLayer(p.B_Cu),(ref,pad.GetNumber())
    for g in fp.GraphicalItems():
        if isinstance(g,p.PCB_TEXT) and g.GetText()=='${REFERENCE}':g.SetVisible(False)
    fps[ref]=fp
for a,b in [((0,0),(WIDTH,0)),((WIDTH,0),(WIDTH,HEIGHT)),((WIDTH,HEIGHT),(0,HEIGHT)),((0,HEIGHT),(0,0))]:
    s=p.PCB_SHAPE();s.SetShape(p.SHAPE_T_SEGMENT);s.SetStart(vec(*a));s.SetEnd(vec(*b));s.SetLayer(p.Edge_Cuts);s.SetWidth(p.FromMM(.05));board.Add(s)
STEP=.1;NX=round(WIDTH/STEP)+1;NY=round(HEIGHT/STEP)+1
PAD_INFO=[];pernet={};pad_masks={};track_masks={};created=[]
def blank():return Image.new('1',(NX,NY))
for ref,fp in fps.items():
    for pad in fp.Pads():
        pos=pad.GetPosition();sz=pad.GetSize();d=dict(ref=ref,num=pad.GetNumber(),x=p.ToMM(pos.x),y=p.ToMM(pos.y),w=p.ToMM(sz.x),h=p.ToMM(sz.y),angle=pad.GetOrientationDegrees(),pad=pad,code=pad.GetNetCode())
        PAD_INFO.append(d);pernet.setdefault(d['code'],[]).append(d)
        im=pad_masks.setdefault(d['code'],blank());a=math.radians(d['angle']);w=d['w']/2+.575;h=d['h']/2+.575
        pts=[((d['x']+dx*math.cos(a)+dy*math.sin(a))/STEP,(d['y']-dx*math.sin(a)+dy*math.cos(a))/STEP) for dx,dy in [(-w,-h),(w,-h),(w,h),(-w,h)]]
        if pad.GetShape()==p.PAD_SHAPE_CIRCLE:ImageDraw.Draw(im).ellipse(((d['x']-w)/STEP,(d['y']-h)/STEP,(d['x']+w)/STEP,(d['y']+h)/STEP),fill=1)
        else:ImageDraw.Draw(im).polygon(pts,fill=1)
def masks(code):
    obs=np.zeros((NY,NX),dtype=bool)
    for src in [pad_masks,track_masks]:
        for n,im in src.items():
            if n!=code:obs|=np.asarray(im,dtype=bool)
    e=9;obs[:e,:]=1;obs[-e:,:]=1;obs[:,:e]=1;obs[:,-e:]=1
    return obs
def cell(d):return (round(d['y']/STEP),round(d['x']/STEP))
DIR=[(0,1,1),(0,-1,1),(1,0,1),(-1,0,1),(1,1,1.4142),(1,-1,1.4142),(-1,1,1.4142),(-1,-1,1.4142)]
def route(a,b,code,tree=None):
    obs=masks(code);start=cell(a);goal=cell(b);starts=tree if tree is not None else {start};obs[goal]=False
    def heur(y,x):
        dy=abs(y-goal[0]);dx=abs(x-goal[1]);return max(dx,dy)+.4142*min(dx,dy)
    dist=np.full((NY,NX),np.inf);queue=[];came={};done=False
    for n in starts:dist[n]=0;obs[n]=False;heapq.heappush(queue,(heur(*n),0,n))
    while queue:
        _,cost,n=heapq.heappop(queue)
        if cost>dist[n]+1e-5:continue
        if n==goal:done=True;break
        y,x=n
        for dy,dx,w in DIR:
            yy=y+dy;xx=x+dx
            if yy<0 or yy>=NY or xx<0 or xx>=NX or obs[yy,xx]:continue
            if dy and dx and (obs[y,xx] or obs[yy,x]):continue
            m=(yy,xx);new=cost+w
            if new+1e-5<dist[m]:dist[m]=new;came[m]=n;heapq.heappush(queue,(new+heur(yy,xx),new,m))
    if not done:raise RuntimeError(f'{a["ref"]}.{a["num"]}->{b["ref"]}.{b["num"]}')
    path=[goal]
    while path[-1] in came:path.append(came[path[-1]])
    return path[::-1]
def seg(a,b,code,width=.6):
    if a==b:return
    t=p.PCB_TRACK(board);t.SetStart(vec(*a));t.SetEnd(vec(*b));t.SetLayer(p.B_Cu);t.SetWidth(p.FromMM(width));t.SetNetCode(code);board.Add(t);created.append(t)
def commit(path,a,b,code):
    im=track_masks.setdefault(code,blank());draw=ImageDraw.Draw(im)
    for d,n in [(a,path[0]),(b,path[-1])]:seg((d['x'],d['y']),(n[1]*STEP,n[0]*STEP),code)
    start=path[0];prev=path[0];dr=None
    for n in path[1:]:
        nd=(n[0]-prev[0],n[1]-prev[1])
        if dr is not None and dr!=nd:seg((start[1]*STEP,start[0]*STEP),(prev[1]*STEP,prev[0]*STEP),code);start=prev
        dr=nd;draw.line((prev[1],prev[0],n[1],n[0]),fill=1,width=18)
        for yy,xx in [prev,n]:draw.ellipse((xx-9,yy-9,xx+9,yy+9),fill=1)
        prev=n
    seg((start[1]*STEP,start[0]*STEP),(prev[1]*STEP,prev[0]*STEP),code)
def pairs(ds):
    connected=[ds[0]];remaining=ds[1:];out=[]
    while remaining:
        _,a,b=min((((a['x']-b['x'])**2+(a['y']-b['y'])**2,a,b) for a in connected for b in remaining),key=lambda v:v[0])
        out.append((a,b));connected.append(b);remaining.remove(b)
    return out
def clear():
    for t in created:board.Remove(t)
    created.clear();track_masks.clear()
def attempt(order,ground=False):
    clear();done=[]
    for name in order:
        if name=='GND' and not ground:continue
        code=nets[name].GetNetCode()
        try:
            ds=pernet[code];tree={cell(ds[0])};linked=[ds[0]];remaining=ds[1:]
            while remaining:
                b=min(remaining,key=lambda d:min((d['x']-a['x'])**2+(d['y']-a['y'])**2 for a in linked))
                path=route(linked[0],b,code,tree)
                start=path[0];a=dict(x=start[1]*STEP,y=start[0]*STEP,ref='TREE',num='0')
                commit(path,a,b,code);tree.update(path);linked.append(b);remaining.remove(b)
        except RuntimeError as e:return done,name,str(e)
        done.append(name)
    return done,None,None
def text(s,x,y,size=.8,layer=p.B_SilkS):
    t=p.PCB_TEXT(board);t.SetText(s);t.SetPosition(vec(x,y));t.SetLayer(layer);t.SetTextSize(vec(size,size));t.SetTextThickness(p.FromMM(.12));t.SetMirrored(layer==p.B_SilkS);board.Add(t);return t
def silk():
    occupied={p.F_SilkS:[],p.B_SilkS:[]}
    def bb(item,margin=0):
        r=item.GetBoundingBox();return(p.ToMM(r.GetLeft())-margin,p.ToMM(r.GetTop())-margin,p.ToMM(r.GetRight())+margin,p.ToMM(r.GetBottom())+margin)
    def overlap(a,b):return a[0]<b[2] and a[2]>b[0] and a[1]<b[3] and a[3]>b[1]
    for fp in fps.values():
        for pad in fp.Pads():occupied[p.B_SilkS].append(bb(pad,.3))
        for g in fp.GraphicalItems():
            if g.GetLayer() in occupied and not isinstance(g,p.PCB_TEXT):occupied[g.GetLayer()].append(bb(g,.15))
    wanted=[]
    for ref,fp in fps.items():
        if ref.startswith('J') and not ref.startswith('JP'):fp.Reference().SetVisible(False);wanted.append((text(fp.GetValue(),COMP[ref]['x'],COMP[ref]['y']),ref))
        else:wanted.append((fp.Reference(),ref))
    for t,ref in wanted:
        layer=t.GetLayer();x=COMP[ref]['x'];y=COMP[ref]['y'];candidates=[]
        for dy in np.arange(-6,6.01,.3):
            for dx in np.arange(-6,6.01,.3):candidates.append((dx*dx+dy*dy,x+dx,y+dy))
        for _,xx,yy in sorted(candidates):
            t.SetPosition(vec(xx,yy));r=bb(t,.1)
            if r[0]<.25 or r[1]<.25 or r[2]>WIDTH-.25 or r[3]>HEIGHT-.25:continue
            if any(overlap(r,b) for b in occupied[layer]):continue
            occupied[layer].append(r);break
        else:print('LABEL SPACE',ref,flush=True)
    for ref,xy in {'C1':(42.5,13),'C2':(17,17),'C3':(19.75,23.5),'C4':(33.5,10),'C5':(33.5,27.3),'C6':(11.75,11.5),'C7':(17.5,30),'U1':(28.5,17)}.items():fps[ref].Reference().SetPosition(vec(*xy))
    text('MonoPod TDA1308 R2',25,2,1,p.F_SilkS)
    text('1 Cu | SMD below | 3.3V',25,4,.8,p.F_SilkS)
    text('ATT: all JP 1-2 | DIRECT: both JP 2-3',28,33.7,.8,p.F_SilkS)
def zone():
    z=p.ZONE(board);z.SetLayer(p.B_Cu);z.SetNet(nets['GND']);z.SetLocalClearance(p.FromMM(.25));z.SetPadConnection(p.ZONE_CONNECTION_FULL)
    z.SetThermalReliefGap(p.FromMM(.3));z.SetThermalReliefSpokeWidth(p.FromMM(.6));z.SetMinThickness(p.FromMM(.4));z.SetIslandRemovalMode(p.ISLAND_REMOVAL_MODE_ALWAYS);z.SetMinIslandArea(0)
    z.Outline().NewOutline()
    for x,y in [(.6,.6),(WIDTH-.6,.6),(WIDTH-.6,HEIGHT-.6),(.6,HEIGHT-.6)]:z.Outline().Append(p.FromMM(x),p.FromMM(y))
    board.Add(z);board.BuildConnectivity();p.ZONE_FILLER(board).Fill(board.Zones())
def save():
    p.SaveBoard(str(ROOT/f'{NAME}.kicad_pcb'),board)
    path=ROOT/f'{NAME}.kicad_pro';pr=json.loads(path.read_text());s=pr['board']['design_settings'];s['rules'].update(engineering);s.pop('rule_severities',None);path.write_text(json.dumps(pr,indent=2),encoding='utf-8')
if __name__=='__main__':
    order=['INV_A','INV_B','AC_A','AC_B','DIRECT_L','DIRECT_R','ATT_L_IN','ATT_R_IN','ATT_L','ATT_R','INA','INB','OUTA_RAW','OUTB_RAW','OUTA_C','OUTB_C','HP_L','HP_R','VREF','VDD','LIN','RIN','GND']
    if '--ground-first' in sys.argv:order.insert(0,order.pop(order.index('GND')))
    best=0;history=[];rng=random.Random(1308)
    for i in range(20):
        done,failed,detail=attempt(order,ground='--ground' in sys.argv)
        print(i,'done',len(done),'failed',failed,detail,flush=True);history.append(dict(attempt=i,done=done,failed=failed,detail=detail,order=order[:]))
        if len(done)>best:best=len(done);save()
        if failed is None:break
        idx=order.index(failed);target=max(0,idx-3-(i%4));order.insert(target,order.pop(idx))
        if i%4==3:
            # Reorder equal-priority local paths to break greedy-router traps.
            a=rng.randrange(max(1,len(order)//2));bidx=rng.randrange(max(1,len(order)//2));order[a],order[bidx]=order[bidx],order[a]
    (ROOT/'validation/routing_attempts.json').write_text(json.dumps(history,indent=2),encoding='utf-8')
    if failed:raise RuntimeError('Single-layer routing incomplete: '+str(failed))
    # Explicit bottom ground connections keep the active THT annulus/mask open even
    # in KiCad export paths that do not rebuild zone-only pad connectivity.
    for ref,end in {'C1':(45.3,17),'C2':(9.2,17),'C3':(18.5,22.6),'J2':(3.8,18.5),'J5':(3.8,22.5)}.items():
        pad=next(d for d in PAD_INFO if d['ref']==ref and d['pad'].GetNetname()=='/GND')
        seg((pad['x'],pad['y']),end,nets['GND'].GetNetCode())
    silk();zone();save();print('ROUTING COMPLETE',len(list(board.GetTracks())),'segments, zero vias',flush=True)
