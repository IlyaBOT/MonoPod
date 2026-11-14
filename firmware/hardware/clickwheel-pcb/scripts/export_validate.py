from pathlib import Path
import os,sys,json,subprocess,hashlib,re,math,xml.etree.ElementTree as ET
import pcbnew as p
from design import ROOT,NAME,COMP
CLI=Path(r'C:\Program Files\KiCad\10.0\bin\kicad-cli.exe')
PCB=ROOT/(NAME+'.kicad_pcb');SCH=ROOT/(NAME+'.kicad_sch')
def run(args):
    a=[str(CLI),*map(str,args)];r=subprocess.run(a,capture_output=True,text=True,encoding='utf-8',errors='replace')
    logs.append(dict(command=a,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr))
    if r.returncode:raise RuntimeError(' '.join(a)+'\n'+r.stdout+'\n'+r.stderr)
    return r
logs=[]
run(['sch','erc','--severity-all','--exit-code-violations','-o',ROOT/'validation/ERC.txt',SCH])
run(['pcb','drc','--severity-all','--exit-code-violations','-o',ROOT/'validation/DRC.txt',PCB])
run(['sch','export','netlist','--format','kicadxml','-o',ROOT/'validation/netlist.xml',SCH])
run(['sch','export','pdf','-o',ROOT/'exports/schematic.pdf',SCH])
run(['sch','export','svg','-o',str(ROOT/'exports/schematic_svg')+os.sep,SCH])
run(['pcb','export','gerbers','-l','F.Cu,F.Paste,F.Mask,B.Mask,F.Silkscreen,Edge.Cuts','--subtract-soldermask','-o',str(ROOT/'gerbers')+os.sep,PCB])
run(['pcb','export','drill','--excellon-separate-th','--generate-map','--map-format','svg','--generate-report','--report-path',ROOT/'validation/drill_report.txt','-o',str(ROOT/'gerbers')+os.sep,PCB])
# KiCad's mandatory virtual B.Cu must not turn into a two-copper manufacturing order.
jobpath=ROOT/'gerbers'/f'{NAME}-job.gbrjob'
(ROOT/'validation/native_job.gbrjob').write_bytes(jobpath.read_bytes())
job=json.loads(jobpath.read_text());job['GeneralSpecs'].update({'LayerNumber':1,'Size':{'X':44.0,'Y':44.0},'Finish':'ENIG'})
job['Header']['GenerationSoftware']['Application']='KiCad / single-copper manufacturing metadata'
job['MaterialStackup']=[x for x in job['MaterialStackup'] if x.get('Name') not in ['B.Cu','Bottom Solder Paste','Bottom Silk Screen']]
for x in job['MaterialStackup']:
    if x['Type']=='Dielectric':x.update({'Name':'FR4 core','Thickness':1.545,'Notes':'Single copper side F.Cu only'})
    if x['Type']=='SolderMask':x['Color']='Green'
jobpath.write_text(json.dumps(job,indent=2),encoding='utf-8')
run(['pcb','export','gerbers','-l','B.Cu','-o',str(ROOT/'validation/back_copper')+os.sep,PCB])
run(['pcb','export','pdf','--mode-single','--black-and-white','--scale','1','-l','F.Cu,F.Silkscreen,Edge.Cuts','-o',ROOT/'exports/pcb_top_1to1.pdf',PCB])
run(['pcb','export','pdf','--mode-single','--black-and-white','--mirror','--scale','1','-l','F.Cu,Edge.Cuts','-o',ROOT/'exports/toner_FCu_MIRRORED_POSITIVE_1to1.pdf',PCB])
run(['pcb','export','svg','--mode-single','--page-size-mode','2','--exclude-drawing-sheet','--scale','1','-l','F.Cu,F.Silkscreen,Edge.Cuts','-o',ROOT/'exports/pcb_top_1to1.svg',PCB])
run(['pcb','export','svg','--mode-single','--page-size-mode','2','--exclude-drawing-sheet','--scale','1','-l','F.Cu,Edge.Cuts','-o',ROOT/'exports/copper_top.svg',PCB])
run(['pcb','export','vrml','--force','--units','mm','--user-origin','100x100mm','-o',ROOT/'exports/assembled.wrl',PCB])
run(['pcb','export','step','--board-only','--force','--user-origin','100x100mm','-o',ROOT/'exports/board_only.step',PCB])
if '--render' in sys.argv:
    run(['pcb','render','--width','1500','--height','1500','--side','top','--background','opaque','--quality','high','--zoom','.83','-o',ROOT/'renders/top.png',PCB])
    run(['pcb','render','--width','1700','--height','1300','--side','top','--rotate','55,0,30','--background','opaque','--quality','high','--zoom','.8','--floor','-o',ROOT/'renders/isometric.png',PCB])
(ROOT/'validation/native_commands.json').write_text(json.dumps(logs,indent=2,ensure_ascii=False),encoding='utf-8')

b=p.LoadBoard(str(PCB));fps={f.GetReference():f for f in b.GetFootprints()}
pro=json.loads((ROOT/(NAME+'.kicad_pro')).read_text())
assert pro['board']['design_settings']['rules']['min_clearance']>=.25
assert pro['board']['design_settings']['rules']['min_track_width']>=.3
assert pro['net_settings']['classes'][0]['clearance']>=.25
assert not pro['board']['design_settings']['drc_exclusions']
assert set(fps)==set(COMP),(set(fps),set(COMP))
tracks=list(b.GetTracks());vias=[t for t in tracks if isinstance(t,p.PCB_VIA)]
assert not vias
assert all(t.GetLayer()==p.F_Cu and p.ToMM(t.GetWidth())>=.3-1e-6 for t in tracks)
assert not [g for g in b.GetDrawings() if g.GetLayer()==p.B_Cu]
assert not [g for f in fps.values() for g in f.GraphicalItems() if g.GetLayer()==p.B_Cu]
assert not [d for f in fps.values() for d in f.Pads() if d.GetAttribute()!=p.PAD_ATTRIB_NPTH and d.FlashLayer(p.B_Cu)]
assert all(z.GetIsRuleArea() for z in b.Zones()),'No copper pour allowed'
edge=[g for g in b.GetDrawings() if g.GetLayer()==p.Edge_Cuts]
assert len(edge)==1 and edge[0].GetShape()==p.SHAPE_T_CIRCLE and abs(p.ToMM(edge[0].GetRadius())-22)<1e-6
actual={}
for xn in ET.parse(ROOT/'validation/netlist.xml').getroot().find('nets'):
    n=xn.attrib['name'].lstrip('/')
    if n.startswith('unconnected-'):continue
    actual[n]=sorted([node.attrib['ref'],node.attrib['pin']] for node in xn.findall('node'))
expected={k:sorted(v) for k,v in json.loads((ROOT/'validation/expected_nets.json').read_text()).items()}
assert actual==expected
geometry={'centre_mm':[100,100],'diameter_mm':44,'thickness_mm':1.6,'components':{},'tracks':[],'silk_text':[]}
lengths={}
for t in tracks:
    n=t.GetNetname().lstrip('/');lengths[n]=lengths.get(n,0)+p.ToMM(t.GetLength())
    geometry['tracks'].append({'net':n,'width':p.ToMM(t.GetWidth()),'a':[p.ToMM(t.GetStart().x)-100,p.ToMM(t.GetStart().y)-100],'b':[p.ToMM(t.GetEnd().x)-100,p.ToMM(t.GetEnd().y)-100]})
for ref,c in COMP.items():
    f=fps[ref];pos=f.GetPosition();x=p.ToMM(pos.x)-100;y=p.ToMM(pos.y)-100
    assert abs(x-c['x'])+abs(y-c['y'])<1e-5
    pads=[]
    for d in f.Pads():
        if d.GetAttribute()!=p.PAD_ATTRIB_NPTH:
            expected_net=c['nets'][d.GetNumber()]
            assert d.GetNetname().lstrip('/')==(expected_net or ''),(ref,d.GetNumber(),d.GetNetname(),expected_net)
        pads.append({'number':d.GetNumber(),'net':d.GetNetname().lstrip('/'),'x':p.ToMM(d.GetPosition().x)-100,'y':p.ToMM(d.GetPosition().y)-100,'w':p.ToMM(d.GetSize().x),'h':p.ToMM(d.GetSize().y),'rotation':d.GetOrientationDegrees(),'drill':p.ToMM(d.GetDrillSize().x),'npth':d.GetAttribute()==p.PAD_ATTRIB_NPTH,'shape':str(d.GetShape())})
    geometry['components'][ref]={**c,'pads':pads}
for ref in ['SW1','SW2','SW3','SW4']:
    c=COMP[ref];assert abs(math.hypot(c['x'],c['y'])-16.5)<1e-9
assert COMP['SW5']['x']==COMP['SW5']['y']==0
for i in range(1,7):assert COMP['R'+str(i)]['value']==['10k','0R','2.2k','4.7k','10k','22k'][i-1]
j=[d for d in geometry['components']['J1']['pads'] if d['drill']>0];assert len(j)==10
for d in j:
    i=int(d['number']);assert abs(d['x']-(-5.08+((i-1)//2)*2.54))<1e-6
    assert abs(d['y']-(11 if i%2 else 8.46))<1e-6
    assert abs(d['drill']-1)<1e-6 and abs(d['w']-1.7)<1e-6
    if i>1:assert d['shape']==str(p.PAD_SHAPE_CIRCLE)
for f in fps.values():
    for t in [f.Reference(),f.Value()]:
        if t.IsVisible():geometry['silk_text'].append({'text':t.GetText(),'x':p.ToMM(t.GetPosition().x)-100,'y':p.ToMM(t.GetPosition().y)-100,'size':p.ToMM(t.GetTextSize().x)})
for t in b.GetDrawings():
    if isinstance(t,p.PCB_TEXT) and t.GetLayer()==p.F_SilkS:geometry['silk_text'].append({'text':t.GetText(),'x':p.ToMM(t.GetPosition().x)-100,'y':p.ToMM(t.GetPosition().y)-100,'size':p.ToMM(t.GetTextSize().x)})
(ROOT/'validation/geometry.json').write_text(json.dumps(geometry,indent=2))

fw=Path(os.environ.get('MONOPOD_FW',r'I:\DIY_iPod_Mono\MonoPod_v0.8.6\DIY_iPod_Mono'))
cfg=(fw/'Config.h').read_text(encoding='utf-8');cpp=(fw/'Input.cpp').read_text(encoding='utf-8');ih=(fw/'Input.h').read_text(encoding='utf-8')
s3=re.search(r'namespace Config\s*\{.*?#if MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3(.*?)#else',cfg,re.S).group(1)
assert re.search(r'PIN_BUTTON_ADC\s*=\s*9\s*;',s3)
assert re.search(r'WHEEL_PINS\[6\]\s*=\s*\{1,2,5,6,7,8\}',s3)
for token in ['analogReadMilliVolts(Config::PIN_BUTTON_ADC)','ADC_11db','mV<200','mV>350 && mV<820','mV>870 && mV<1290','mV>1440 && mV<1840','mV>2010 && mV<2580','millis()-changed>=35','(float)raw[i]-(float)base[i]']:
    assert token in cpp,token
assert 'uint32_t raw[6]' in ih
firmware={'source_directory':str(fw),'profile':'MONOPOD_BOARD_ESP32_S3','ADC_GPIO':9,'touch_GPIOs':[1,2,5,6,7,8],'attenuation':'ADC_11db','debounce_ms':35,'files':{n:hashlib.sha256((fw/n).read_bytes()).hexdigest() for n in ['Config.h','Input.cpp','Input.h']}}
(ROOT/'validation/firmware_match.json').write_text(json.dumps(firmware,indent=2))
adc=[]
for name,r,lo,hi in [('MENU',0,0,200),('PREV',2200,350,820),('NEXT',4700,870,1290),('PLAY',10000,1440,1840),('SELECT',22000,2010,2580)]:
    v=3300*r/(10000+r);vmin=3135*(r*.99)/(10100+r*.99);vmax=3465*(r*1.01)/(9900+r*1.01)
    assert (vmin>=lo if name=='MENU' else vmin>lo) and vmax<hi
    tau=(10000*r/(10000+r))*1e-7 if r else 0
    adc.append(dict(button=name,R_ohm=r,mV=v,min_mV_with_1pct_R_and_5pct_supply=vmin,max_mV_with_1pct_R_and_5pct_supply=vmax,firmware_window_mV=[lo,hi],press_tau_ms=tau*1000))
(ROOT/'validation/adc_calculation.json').write_text(json.dumps({'buttons':adc,'idle_mV':3300,'release_tau_ms':1,'worst_release_5tau_ms':5.555,'note':'Calculation only; ADC calibration, supply, wiring and actual switches require bench measurement.'},indent=2))
for suffix,count,drill in [('PTH',10,1.0),('NPTH',4,2.2)]:
    content=(ROOT/'gerbers'/f'{NAME}-{suffix}.drl').read_text()
    hits=re.findall(r'^X(-?[0-9.]+)Y(-?[0-9.]+)$',content,re.M)
    assert len(hits)==count,(suffix,len(hits));assert f'C{drill:.3f}' in content
back=(ROOT/'validation/back_copper'/f'{NAME}-B_Cu.gbl').read_text()
assert not re.search(r'D0[13]\*',back),'Back copper must have no draws/flashes'
checks={'kicad':'10.0.6','ERC_violations':0,'DRC_violations':0,'unconnected':0,'vias':0,'B_Cu_tracks':0,'B_Cu_flashes':0,'B_Cu_graphics':0,'copper_pours':0,'switch_coordinates':'PASS','J1_pinout':'PASS','schematic_vs_PCB_nets':'PASS','resistor_values':'PASS','firmware_v086_S3':'PASS','Excellon_PTH_hits':10,'Excellon_NPTH_hits':4,'track_count':len(tracks),'track_widths_mm':sorted(set(p.ToMM(t.GetWidth()) for t in tracks)),'trace_lengths_mm':{k:round(v,3) for k,v in lengths.items()},'physical_mechanics':'UNVERIFIED','kit_switch_land_match':'UNVERIFIED','live_ESP32_ADC_and_touch':'UNVERIFIED'}
(ROOT/'validation/checks.json').write_text(json.dumps(checks,indent=2))
print(json.dumps(checks,indent=2))
