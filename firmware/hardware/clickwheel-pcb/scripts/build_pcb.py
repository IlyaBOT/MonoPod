import os,math,json,heapq,random,sys
from pathlib import Path
import xml.etree.ElementTree as ET
import numpy as np
from PIL import Image,ImageDraw
import pcbnew as p
from design import ROOT,NAME,COMP,ROOTUUID
os.environ['KIPRJMOD']=str(ROOT)
vec=lambda x,y:p.VECTOR2I(p.FromMM(float(x)),p.FromMM(float(y)))
board=p.BOARD();board.SetCopperLayerCount(2)
board.GetDesignSettings().SetBoardThickness(p.FromMM(1.6))
board.GetDesignSettings().m_SolderMaskExpansion=p.FromMM(.05)
board.GetDesignSettings().m_SolderMaskMinWidth=p.FromMM(.1)
tb=p.TITLE_BLOCK();tb.SetTitle('MonoPod Click Wheel - F.Cu only');tb.SetRevision('R1');tb.SetDate('2026-10-29');board.SetTitleBlock(tb)
nets={};padnets={};actual={}
for xn in ET.parse(ROOT/'validation/netlist.xml').getroot().find('nets'):
    n=xn.attrib['name'];bare=n.lstrip('/')
    if bare.startswith('unconnected-'):continue
    net=p.NETINFO_ITEM(board,n,int(xn.attrib['code']));board.Add(net);nets[bare]=net;actual[bare]=[]
    for node in xn.findall('node'):
        padnets[node.attrib['ref'],node.attrib['pin']]=net;actual[bare].append([node.attrib['ref'],node.attrib['pin']])
assert {k:sorted(v) for k,v in actual.items()}=={k:sorted(v) for k,v in json.loads((ROOT/'validation/expected_nets.json').read_text()).items()}
fps={}
for ref,c in COMP.items():
    lib,n=c['fp'].split(':');fp=p.FootprintLoad(str(ROOT/'libraries'/f'{lib}.pretty'),n);assert fp
    fp.SetReference(ref);fp.SetValue(c['value']);fp.SetFPID(p.LIB_ID(lib,n));fp.SetPath(p.KIID_PATH('/'+ROOTUUID+'/'+c['uuid']))
    board.Add(fp);fp.SetPosition(vec(100+c['x'],100+c['y']));fp.SetOrientationDegrees(c['rot']);fp.Value().SetVisible(False)
    fp.Reference().SetTextSize(vec(.8,.8));fp.Reference().SetTextThickness(p.FromMM(.12));fp.Reference().SetTextAngle(p.EDA_ANGLE(0,p.DEGREES_T))
    for pad in fp.Pads():
        key=(ref,pad.GetNumber())
        if key in padnets:pad.SetNet(padnets[key])
    for g in fp.GraphicalItems():
        if isinstance(g,p.PCB_TEXT) and g.GetText()=='${REFERENCE}':g.SetVisible(False)
    fps[ref]=fp
def shape_circle(x,y,r,layer,w=.1):
    s=p.PCB_SHAPE();s.SetShape(p.SHAPE_T_CIRCLE);s.SetCenter(vec(100+x,100+y));s.SetEnd(vec(100+x+r,100+y));s.SetLayer(layer);s.SetWidth(p.FromMM(w));board.Add(s)
def line(a,b,layer=p.Dwgs_User,w=.1):
    s=p.PCB_SHAPE();s.SetShape(p.SHAPE_T_SEGMENT);s.SetStart(vec(100+a[0],100+a[1]));s.SetEnd(vec(100+b[0],100+b[1]));s.SetLayer(layer);s.SetWidth(p.FromMM(w));board.Add(s)
def text(t,x,y,size=.8,layer=p.F_SilkS):
    s=p.PCB_TEXT(board);s.SetText(t);s.SetPosition(vec(100+x,100+y));s.SetLayer(layer);s.SetTextSize(vec(size,size));s.SetTextThickness(p.FromMM(.12));board.Add(s);return s
shape_circle(0,0,22,p.Edge_Cuts,.05)
shape_circle(0,0,22,p.Dwgs_User,.1);shape_circle(0,0,7.5,p.Dwgs_User,.1)
line((-23,0),(23,0));line((0,-23),(0,23))
for ref in ['SW1','SW2','SW3','SW4','SW5']:
    c=COMP[ref];shape_circle(c['x'],c['y'],1.5,p.Dwgs_User,.1)
    text(ref+' PUSHER D3',c['x'],c['y']-4,.65,p.Dwgs_User)
for ref in ['H1','H2','H3','H4']:
    c=COMP[ref];line((c['x']-1,c['y']),(c['x']+1,c['y']));line((c['x'],c['y']-1),(c['x'],c['y']+1))
for x,y in [(-8,-8),(8,-8),(-10,10),(10,10)]:
    shape_circle(x,y,1.15,p.Dwgs_User,.1);text('STOP',x,y,.55,p.Dwgs_User)
text('WHEEL D44 / SELECT D15 / nominal switch top Z=0.8',0,-25,1,p.Dwgs_User)
text('Mounts +/-13mm | confirm plastic, stop height and kit terminals',0,25,1,p.Dwgs_User)

STEP=.1;NX=NY=441
PAD_INFO=[];groups={};pad_masks={};track_masks={};created=[]
blank=lambda:Image.new('1',(NX,NY))
def raster_pad(im,d,margin):
    draw=ImageDraw.Draw(im);a=math.radians(d['angle']);w=d['w']/2+margin;h=d['h']/2+margin
    if d['shape']==p.PAD_SHAPE_CIRCLE:
        draw.ellipse(((d['x']-w)/STEP,(d['y']-h)/STEP,(d['x']+w)/STEP,(d['y']+h)/STEP),fill=1)
    else:
        pts=[((d['x']+dx*math.cos(a)+dy*math.sin(a))/STEP,(d['y']-dx*math.sin(a)+dy*math.cos(a))/STEP) for dx,dy in [(-w,-h),(w,-h),(w,h),(-w,h)]]
        draw.polygon(pts,fill=1)
for ref,fp in fps.items():
    for pad in fp.Pads():
        pos=pad.GetPosition();sz=pad.GetSize();code=pad.GetNetCode()
        d=dict(ref=ref,num=pad.GetNumber(),x=p.ToMM(pos.x)-78,y=p.ToMM(pos.y)-78,w=p.ToMM(sz.x),h=p.ToMM(sz.y),angle=pad.GetOrientationDegrees(),shape=pad.GetShape(),pad=pad,code=code)
        PAD_INFO.append(d);key=code if code else -len(PAD_INFO)
        if ref.startswith('H'):d['w']=d['h']=5.0
        raster_pad(pad_masks.setdefault(key,blank()),d,.45)
        if code:groups.setdefault(code,{}).setdefault((ref,d['num']),[]).append(d)
fixed=np.zeros((NY,NX),dtype=bool)
yy,xx=np.indices((NY,NX));fixed|=((xx*STEP-22)**2+(yy*STEP-22)**2>(22-.72)**2)
# Avoid copper directly under the uninsulated switch centre. Solder-mask remains mandatory there.
for c in [COMP['SW'+str(i)] for i in range(1,6)]:
    cx=round((c['x']+22)/STEP);cy=round((c['y']+22)/STEP)
    fixed[cy-12:cy+13,cx-17:cx+18]=True
def masks(code):
    obs=fixed.copy()
    for src in [pad_masks,track_masks]:
        for n,im in src.items():
            if n!=code:obs|=np.asarray(im,dtype=bool)
    return obs
def cell(d):return(round(d['y']/STEP),round(d['x']/STEP))
DIR=[(0,1,1),(0,-1,1),(1,0,1),(-1,0,1),(1,1,1.4142),(1,-1,1.4142),(-1,1,1.4142),(-1,-1,1.4142)]
def route(starts,targets,code):
    obs=masks(code);goals={cell(d) for d in targets}
    def h(y,x):return min(max(abs(x-gx),abs(y-gy))+.4142*min(abs(x-gx),abs(y-gy)) for gy,gx in goals)
    dist=np.full((NY,NX),np.inf);queue=[];came={}
    for n in starts:dist[n]=0;obs[n]=False;heapq.heappush(queue,(h(*n),0,n))
    for n in goals:obs[n]=False
    done=None
    while queue:
        _,cost,n=heapq.heappop(queue)
        if cost>dist[n]+1e-5:continue
        if n in goals:done=n;break
        y,x=n
        for dy,dx,w in DIR:
            yy=y+dy;xx=x+dx
            if yy<0 or yy>=NY or xx<0 or xx>=NX or obs[yy,xx]:continue
            if dy and dx and (obs[y,xx] or obs[yy,x]):continue
            m=(yy,xx);new=cost+w
            if new+1e-5<dist[m]:dist[m]=new;came[m]=n;heapq.heappush(queue,(new+h(yy,xx),new,m))
    if done is None:raise RuntimeError('No single-side route to '+str([(d['ref'],d['num']) for d in targets]))
    path=[done]
    while path[-1] in came:path.append(came[path[-1]])
    return path[::-1]
def seg(a,b,code,width=.3):
    if abs(a[0]-b[0])+abs(a[1]-b[1])<1e-7:return
    if nets['GND'].GetNetCode()==code or nets['3V3'].GetNetCode()==code:width=.35
    t=p.PCB_TRACK(board);t.SetStart(vec(a[0]+78,a[1]+78));t.SetEnd(vec(b[0]+78,b[1]+78));t.SetLayer(p.F_Cu);t.SetWidth(p.FromMM(width));t.SetNetCode(code);board.Add(t);created.append(t)
def commit(path,targets,code):
    im=track_masks.setdefault(code,blank());draw=ImageDraw.Draw(im)
    last=path[-1];target=next(d for d in targets if cell(d)==last)
    seg((target['x'],target['y']),(last[1]*STEP,last[0]*STEP),code)
    start=prev=path[0];dr=None
    for n in path[1:]:
        nd=(n[0]-prev[0],n[1]-prev[1])
        if dr is not None and dr!=nd:seg((start[1]*STEP,start[0]*STEP),(prev[1]*STEP,prev[0]*STEP),code);start=prev
        dr=nd;draw.line((prev[1],prev[0],n[1],n[0]),fill=1,width=13)
        for y,x in [prev,n]:draw.ellipse((x-6.2,y-6.2,x+6.2,y+6.2),fill=1)
        prev=n
    seg((start[1]*STEP,start[0]*STEP),(prev[1]*STEP,prev[0]*STEP),code)
def clear():
    for t in created:board.Remove(t)
    created.clear();track_masks.clear()
prepaths={}
def preconnect():
    prepaths.clear()
    for code,gs in groups.items():
        for key,ds in gs.items():
            if len(ds)<2:continue
            a,b=sorted(ds,key=lambda d:d['x']);ac=cell(a);bc=cell(b)
            assert ac[0]==bc[0]
            path=[(ac[0],x) for x in range(ac[1],bc[1]+1)]
            commit(path,ds,code)
            for d in ds:seg((d['x'],d['y']),(cell(d)[1]*STEP,cell(d)[0]*STEP),code)
            prepaths[key]=set(path)
def attempt(order):
    clear();preconnect();done=[]
    for name in order:
        code=nets[name].GetNetCode();gs=list(groups[code].values())
        # Start at J1 if present, or the first component terminal.
        gs.sort(key=lambda g:(0 if g[0]['ref']=='J1' else 1,g[0]['ref'],g[0]['num']))
        linked=list(gs.pop(0));tree={cell(d) for d in linked};tree.update(prepaths.get((linked[0]['ref'],linked[0]['num']),set()))
        # Exact-to-grid stubs inside pads.
        for d in linked:seg((d['x'],d['y']),(cell(d)[1]*STEP,cell(d)[0]*STEP),code)
        while gs:
            target=min(gs,key=lambda g:min((a['x']-b['x'])**2+(a['y']-b['y'])**2 for a in linked for b in g))
            try:path=route(tree,target,code)
            except RuntimeError as e:return done,name,str(e)
            commit(path,target,code);tree.update(path)
            for d in target:
                n=cell(d);tree.add(n);seg((d['x'],d['y']),(n[1]*STEP,n[0]*STEP),code)
            tree.update(prepaths.get((target[0]['ref'],target[0]['num']),set()))
            linked+=target;gs.remove(target)
        done.append(name)
    return done,None,None
def silk():
    occupied=[]
    def bb(item,margin=0):
        r=item.GetBoundingBox();return(p.ToMM(r.GetLeft())-margin,p.ToMM(r.GetTop())-margin,p.ToMM(r.GetRight())+margin,p.ToMM(r.GetBottom())+margin)
    def overlap(a,b):return a[0]<b[2] and a[2]>b[0] and a[1]<b[3] and a[3]>b[1]
    for fp in fps.values():
        for pad in fp.Pads():occupied.append(bb(pad,.25))
        for g in fp.GraphicalItems():
            if g.GetLayer()==p.F_SilkS and not isinstance(g,p.PCB_TEXT):occupied.append(bb(g,.08))
            if g.GetLayer()==p.F_CrtYd and isinstance(g,p.PCB_SHAPE):occupied.append(bb(g,.08))
    for t in [text('MonoPod R1',0,-5,.8),text('F.Cu ONLY',0,-7,.8),text('1',-7.2,11,.8)]:occupied.append(bb(t,.15))
    wanted=[]
    for ref,fp in fps.items():
        c=COMP[ref]
        if ref.startswith('H'):fp.Reference().SetVisible(False);continue
        wanted.append((fp.Reference(),c['x']+100,c['y']+100))
        if ref.startswith('SW'):wanted.append((text(c['value'],c['x'],c['y'],.8),c['x']+100,c['y']+100))
    for t,x,y in wanted:
        candidates=[]
        for dy in np.arange(-8,8.01,.25):
            for dx in np.arange(-8,8.01,.25):candidates.append((dx*dx+dy*dy,x+dx,y+dy))
        for _,xx,yy in sorted(candidates):
            t.SetPosition(vec(xx,yy));r=bb(t,.1)
            if any((cx-100)**2+(cy-100)**2>21.5**2 for cx,cy in [(r[0],r[1]),(r[2],r[3]),(r[0],r[3]),(r[2],r[1])]):continue
            if any(overlap(r,b) for b in occupied):continue
            occupied.append(r);break
        else:print('LABEL SPACE',t.GetText(),flush=True)
    # The full signal map is on the assembly drawing; numbering is visible on F.Fab.
def mechanical_rules():
    def area(x,y,r,head):
        z=p.ZONE(board);z.SetLayer(p.F_Cu);z.SetIsRuleArea(True)
        z.SetDoNotAllowTracks(head);z.SetDoNotAllowVias(True);z.SetDoNotAllowPads(False);z.SetDoNotAllowZoneFills(True);z.SetDoNotAllowFootprints(not head)
        z.Outline().NewOutline()
        for i in range(48):
            a=i*2*math.pi/48;z.Outline().Append(p.FromMM(100+x+r*math.cos(a)),p.FromMM(100+y+r*math.sin(a)))
        board.Add(z)
    for ref in ['H1','H2','H3','H4']:area(COMP[ref]['x'],COMP[ref]['y'],2.5,True)
    for x,y in [(-8,-8),(8,-8),(-10,10),(10,10)]:area(x,y,1.15,False)

if __name__=='__main__':
    order=['TOUCH1','TOUCH2','TOUCH3','TOUCH4','TOUCH5','TOUCH6','MENU_RET','PREV_RET','NEXT_RET','PLAY_RET','SELECT_RET','3V3','GND','BTN_ADC']
    best=0;history=[];rng=random.Random(1187)
    for i in range(45):
        done,failed,detail=attempt(order);history.append(dict(attempt=i,order=order.copy(),done=done,failed=failed,detail=detail));print(i,len(done),failed,detail,flush=True)
        if len(done)>best:
            best=len(done);p.SaveBoard(str(ROOT/'validation/best_partial.kicad_pcb'),board)
        if not failed:break
        # Move failed net earlier; randomize when a local ordering optimum repeats.
        order.remove(failed);order.insert(max(0,len(done)-rng.randrange(2,6)),failed)
        if i%9==8:rng.shuffle(order)
    (ROOT/'validation/routing_history.json').write_text(json.dumps(history,indent=2))
    if failed:raise RuntimeError('Routing incomplete')
    silk();mechanical_rules();board.BuildConnectivity()
    # Remove unused PTH annuli, including B.Cu. NC front land is a stacked SMD pad.
    for pad in fps['J1'].Pads():
        if pad.GetAttribute()==p.PAD_ATTRIB_PTH:pad.SetUnconnectedLayerMode(p.UNCONNECTED_LAYER_MODE_REMOVE_ALL)
    p.SaveBoard(str(ROOT/f'{NAME}.kicad_pcb'),board)
    # Save the exact canonical pad-stack representation used by KiCad 10.
    saved=p.LoadBoard(str(ROOT/f'{NAME}.kicad_pcb'))
    p.FootprintSave(str(ROOT/'libraries/ClickWheel.pretty'),next(f for f in saved.GetFootprints() if f.GetReference()=='J1'))
    # pcbnew SaveBoard serializes default project rules unless explicitly restored.
    pro=ROOT/f'{NAME}.kicad_pro';pr=json.loads(pro.read_text(encoding='utf-8'))
    pr['board']['design_settings']['rules'].update({'min_clearance':.25,'min_track_width':.3,'min_via_diameter':.6,'min_through_hole_diameter':.3,'min_hole_to_hole':.25,'min_copper_edge_clearance':.5,'min_silk_clearance':.15,'min_text_height':.8,'min_text_thickness':.1})
    pr['board']['design_settings']['drc_exclusions']=[]
    pr['net_settings']['classes'][0].update({'clearance':.25,'track_width':.3})
    pro.write_text(json.dumps(pr,indent=2),encoding='utf-8')
    (ROOT/'validation/routing_metrics.json').write_text(json.dumps({'tracks':len(created),'vias':0,'layers':['F.Cu'],'min_width_mm':.3,'ground_zone':False,'attempts':len(history)},indent=2))
    print('COMPLETE',len(created),'segments',flush=True)
