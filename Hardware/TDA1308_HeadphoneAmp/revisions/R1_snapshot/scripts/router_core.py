PAD_INFO=[]; pernet={}
for ref,fp in fps.items():
    for pad in fp.Pads():
        pos=pad.GetPosition(); x=p.ToMM(pos.x);y=p.ToMM(pos.y)
        size=pad.GetSize();w=p.ToMM(size.x);h=p.ToMM(size.y)
        a=pad.GetOrientationDegrees()
        nc=pad.GetNetCode(); code=nc if nc else -len(PAD_INFO)-1
        ls=[l for l,L in enumerate(layers) if pad.IsOnLayer(L)]
        d={'ref':ref,'num':pad.GetNumber(),'x':x,'y':y,'w':w,'h':h,'angle':a,'ls':ls,'code':code,'nc':nc,'pad':pad}
        PAD_INFO.append(d)
        if nc:pernet.setdefault(nc,[]).append(d)

STEP=.10;NX=int(WIDTH/STEP)+1;NY=int(HEIGHT/STEP)+1
pad_track={};pad_via={};other_tracks={};other_vias={}
def blank():return Image.new('1',(NX,NY))
def polygon_pad(im,d,margin):
    a=math.radians(d['angle']);x=d['x'];y=d['y']
    if d['ref'].startswith('H'):
        ImageDraw.Draw(im).ellipse(((x-3.6)/STEP,(y-3.6)/STEP,(x+3.6)/STEP,(y+3.6)/STEP),fill=1);return
    # Conservative rounded rectangle represented as rectangle, including drills.
    w=d['w']/2+margin;h=d['h']/2+margin
    pts=[((x+dx*math.cos(a)+dy*math.sin(a))/STEP,(y-dx*math.sin(a)+dy*math.cos(a))/STEP) for dx,dy in [(-w,-h),(w,-h),(w,h),(-w,h)]]
    ImageDraw.Draw(im).polygon(pts,fill=1)
for d in PAD_INFO:
    for target,margin in [(pad_track,.38),(pad_via,.59)]:
        target.setdefault(d['code'],[blank(),blank()])
        for l in d['ls']:polygon_pad(target[d['code']][l],d,margin)

def masks(code):
    obs=np.zeros((2,NY,NX),dtype=bool);vobs=obs.copy()
    for src,dst in [(pad_track,obs),(pad_via,vobs),(other_tracks,obs),(other_vias,vobs)]:
        for n,ims in src.items():
            if n!=code:
                for l in range(2):dst[l]|=np.asarray(ims[l],dtype=bool)
    # 0.5 edge clearance + half trace, with raster reserve.
    for dst,edge in [(obs,7),(vobs,9)]:
        dst[:,:edge,:]=1;dst[:,-edge:,:]=1;dst[:,:,:edge]=1;dst[:,:,-edge:]=1
    return obs,vobs

def cell(d,l):return (l,int(round(d['y']/STEP)),int(round(d['x']/STEP)))
DIR=[(0,1,1),(0,-1,1),(1,0,1),(-1,0,1),(1,1,1.4142),(1,-1,1.4142),(-1,1,1.4142),(-1,-1,1.4142)]
def route(a,b,code,prefer,front_only=False):
    obs,vobs=masks(code); goals={cell(b,l) for l in b['ls']};starts=[cell(a,l) for l in a['ls']]
    for n in starts+list(goals):obs[n]=False
    def heuristic(y,x):
        dx=abs(x-int(round(b['x']/STEP)));dy=abs(y-int(round(b['y']/STEP)))
        return max(dx,dy)+.4142*min(dx,dy)
    dist=np.full((2,NY,NX),np.inf,dtype=np.float64);came={};queue=[]
    for node in starts:dist[node]=0;heapq.heappush(queue,(heuristic(node[1],node[2]),0,node))
    done=None;nodes=0
    while queue:
        _,cost,n=heapq.heappop(queue)
        if cost>dist[n]+1e-5:continue
        if n in goals:done=n;break
        l,y,x=n;nodes+=1
        if nodes>700000:break
        for dy,dx,w in DIR:
            yy=y+dy;xx=x+dx
            if yy<0 or yy>=NY or xx<0 or xx>=NX or obs[l,yy,xx]:continue
            if dy and dx and (obs[l,y,xx] or obs[l,yy,x]):continue
            m=(l,yy,xx);new=cost+w*(1.0 if l==prefer else 1.7)
            if new+1e-5<dist[m]:dist[m]=new;came[m]=n;heapq.heappush(queue,(new+heuristic(yy,xx),new,m))
        ol=1-l
        if not front_only and not vobs[:,y,x].any() and not obs[ol,y,x]:
            m=(ol,y,x);new=cost+110
            if new+1e-5<dist[m]:dist[m]=new;came[m]=n;heapq.heappush(queue,(new+heuristic(y,x),new,m))
    if done is None:
        if front_only:raise RuntimeError('No front-only path')
        p.SaveBoard(str(ROOT/f'{NAME}.kicad_pcb'),board)
        for goal in list(goals)+starts:
            l,gy,gx=goal
            print('Blocked neighborhood',goal,obs[l,gy-2:gy+3,gx-2:gx+3].astype(int),flush=True)
            for src in [pad_track,other_tracks]:
                for n,ims in src.items():
                    if n!=code and np.asarray(ims[l])[gy-2:gy+3,gx-2:gx+3].any():print('Nearby obstacle net',n,flush=True)
        raise RuntimeError(f'ROUTING FAILED: {a["ref"]}:{a["num"]} -> {b["ref"]}:{b["num"]}, expanded {nodes}')
    path=[done]
    while path[-1] in came:path.append(came[path[-1]])
    path.reverse()
    return path

def addsegment(a,b,code,width=.25):
    if a==b:return
    t=p.PCB_TRACK(board);t.SetStart(vec(a[2]*STEP,a[1]*STEP));t.SetEnd(vec(b[2]*STEP,b[1]*STEP));t.SetLayer(layers[a[0]]);t.SetWidth(p.FromMM(width));t.SetNetCode(code);board.Add(t)
def commit(path,a,b,code):
    other_tracks.setdefault(code,[blank(),blank()]);other_vias.setdefault(code,[blank(),blank()])
    # Exact pad centers connected to grid; entire tiny extension remains inside land.
    for d,n in [(a,path[0]),(b,path[-1])]:
        t=p.PCB_TRACK(board);t.SetStart(vec(d['x'],d['y']));t.SetEnd(vec(n[2]*STEP,n[1]*STEP));t.SetLayer(layers[n[0]]);t.SetWidth(p.FromMM(.25));t.SetNetCode(code)
        if t.GetStart()!=t.GetEnd():board.Add(t)
    start=path[0];prev=path[0];direction=None
    for n in path[1:]:
        if n[0]!=prev[0]:
            addsegment(start,prev,code)
            v=p.PCB_VIA(board);v.SetPosition(vec(n[2]*STEP,n[1]*STEP));v.SetWidth(p.FromMM(.65));v.SetDrill(p.FromMM(.3));v.SetViaType(p.VIATYPE_THROUGH);v.SetLayerPair(p.F_Cu,p.B_Cu);v.SetNetCode(code);board.Add(v)
            for target,r in [(other_tracks,.72),(other_vias,.94)]:
                for l in range(2):ImageDraw.Draw(target[code][l]).ellipse((n[2]-r/STEP,n[1]-r/STEP,n[2]+r/STEP,n[1]+r/STEP),fill=1)
            start=n;direction=None
        else:
            dr=(n[1]-prev[1],n[2]-prev[2])
            if direction is not None and dr!=direction:addsegment(start,prev,code);start=prev
            direction=dr
            for target,r in [(other_tracks,.50),(other_vias,.74)]:
                draw=ImageDraw.Draw(target[code][n[0]])
                draw.line((prev[2],prev[1],n[2],n[1]),fill=1,width=int(math.ceil(2*r/STEP)))
                for pt in [prev,n]:draw.ellipse((pt[2]-r/STEP,pt[1]-r/STEP,pt[2]+r/STEP,pt[1]+r/STEP),fill=1)
        prev=n
    addsegment(start,prev,code)

def pairs(ds):
    connected=[ds[0]];remaining=ds[1:];out=[]
    while remaining:
        _,a,b=min((((a['x']-b['x'])**2+(a['y']-b['y'])**2,a,b) for a in connected for b in remaining),key=lambda v:v[0])
        out.append((a,b));connected.append(b);remaining.remove(b)
    return out

