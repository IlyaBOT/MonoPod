from pathlib import Path
import subprocess,shutil,json
ROOT=Path(__file__).resolve().parents[1];NAME='TDA1308_HeadphoneAmp'
CLI=shutil.which('kicad-cli') or r'C:\Program Files\KiCad\10.0\bin\kicad-cli.exe'
runs=[]
def run(args):
    r=subprocess.run([CLI]+[str(x) for x in args],cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace')
    runs.append(dict(command=[CLI]+[str(x) for x in args],returncode=r.returncode,stdout=r.stdout,stderr=r.stderr))
    assert r.returncode==0,r.stderr
    print('PASS',' '.join(str(x) for x in args[:3]),flush=True)
board=ROOT/f'{NAME}.kicad_pcb';sch=ROOT/f'{NAME}.kicad_sch'
run(['pcb','export','gerbers','-l','F.Cu,B.Cu,F.Mask,B.Mask,F.Silkscreen,B.Silkscreen,Edge.Cuts','-o',str(ROOT/'gerbers')+'/',board])
run(['pcb','export','drill','--format','excellon','--excellon-units','mm','--excellon-separate-th','--generate-map','--map-format','svg','--generate-report','--report-path',ROOT/'validation/drill_report.txt','-o',str(ROOT/'gerbers')+'/',board])
for name,layers,mirror in [('copper_front','F.Cu,F.Silkscreen,Edge.Cuts',False),('copper_back','B.Cu,B.Silkscreen,Edge.Cuts',True),('assembly','F.Fab,F.Silkscreen,Edge.Cuts',False)]:
    run(['pcb','export','svg','-l',layers,'--mode-single','--fit-page-to-board','--exclude-drawing-sheet',*(['--mirror'] if mirror else []),'-o',ROOT/f'renders/{name}.svg',board])
for side,name in [('top','front'),('bottom','back')]:run(['pcb','render','--width','1800','--height','1200','--side',side,'--background','opaque','-o',ROOT/f'renders/{name}.png',board])
run(['sch','export','svg','-o',str(ROOT/'renders')+'/',sch])
run(['sch','export','pdf','-o',ROOT/'docs/schematic.pdf',sch])
(ROOT/'validation/export_runs.json').write_text(json.dumps(runs,indent=2),encoding='utf-8')
