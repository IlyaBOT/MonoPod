from pathlib import Path
import subprocess,shutil,json,hashlib,re,sys
ROOT=Path(__file__).resolve().parents[1];NAME='TDA1308_HeadphoneAmp'
CLI=shutil.which('kicad-cli') or r'C:\Program Files\KiCad\10.0\bin\kicad-cli.exe'
runs=[]
def run(args):
    r=subprocess.run([CLI]+[str(x) for x in args],cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace')
    runs.append(dict(command=[CLI]+[str(x) for x in args],returncode=r.returncode,stdout=r.stdout,stderr=r.stderr))
    assert r.returncode==0,r.stderr
    print('PASS',' '.join(str(x) for x in args[:3]),flush=True)
board=ROOT/f'{NAME}.kicad_pcb';sch=ROOT/f'{NAME}.kicad_sch'
run(['pcb','export','gerbers','--no-x2','--no-netlist','-l','B.Cu,B.Mask,F.Silkscreen,B.Silkscreen,Edge.Cuts','-o',str(ROOT/'gerbers')+'/',board])
normalizations=[]
def geometry(s):return '\n'.join(x for x in s.splitlines() if not x.startswith('G04'))
for path in (ROOT/'gerbers').iterdir():
    if path.suffix=='.gbrjob':
        # KiCad always describes two logical layers. Do not supply that internal job to a one-copper-side fabricator.
        path.unlink();continue
    if path.suffix not in ['.gbl','.gbs','.gbo','.gto','.gm1']:continue
    src=path.read_text(encoding='ascii');dst='\n'.join(x for x in src.splitlines() if not x.startswith('G04 #@! '))+'\n'
    assert geometry(src)==geometry(dst)
    path.write_text(dst,encoding='ascii')
    normalizations.append(dict(file=path.name,geometry_sha256=hashlib.sha256(geometry(dst).encode()).hexdigest(),removed_x2_comment_lines=sum(x.startswith('G04 #@! ') for x in src.splitlines())))
(ROOT/'validation/gerber_metadata_normalization.json').write_text(json.dumps(normalizations,indent=2),encoding='utf-8')
run(['pcb','export','drill','--format','excellon','--excellon-min-header','--excellon-units','mm','--generate-map','--map-format','svg','--generate-report','--report-path',ROOT/'validation/drill_report_from_KiCad.txt','-o',str(ROOT/'gerbers')+'/',board])
# An empty F.Cu plot is evidence only; never include it in the fabrication package.
proof=ROOT/'validation/empty_front_copper_proof';proof.mkdir(exist_ok=True)
run(['pcb','export','gerbers','--no-x2','--no-netlist','-l','F.Cu','-o',str(proof)+'/',board])
front=(proof/f'{NAME}-F_Cu.gtl').read_text(encoding='ascii')
assert not re.search(r'D0[13]\*',front),'Unexpected top-side copper artwork'
assert '%ADD' not in front,'Unexpected aperture in top copper proof'
for path in proof.glob('*.gbrjob'):path.unlink()
for name,layers,mirror in [('copper_bottom_view','B.Cu,B.Silkscreen,Edge.Cuts',True),('assembly_bottom','B.Fab,B.Silkscreen,Edge.Cuts',True),('assembly_top','F.Fab,F.Silkscreen,Edge.Cuts',False)]:
    run(['pcb','export','svg','-l',layers,'--mode-single','--fit-page-to-board','--exclude-drawing-sheet',*(['--mirror'] if mirror else []),'-o',ROOT/f'renders/{name}.svg',board])
for side,name in [('top','front'),('bottom','back')]:run(['pcb','render','--width','1800','--height','1300','--side',side,'--background','opaque','-o',ROOT/f'renders/{name}.png',board])
if '--board-only' not in sys.argv:
    run(['sch','export','svg','-o',str(ROOT/'renders')+'/',sch])
    pdf=ROOT/'docs/schematic.pdf'
    if not pdf.exists():
        subprocess.run([r'C:\Users\IlyaBOT\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe',r'C:\Users\IlyaBOT\.codex\plugins\cache\openai-primary-runtime\pdf\26.1025.11240\skills\pdf\container_tools\mark_artifact_operation_started.mjs','--operation-kind','create','--expected-output-count','1','--output-format','pdf'],check=True)
    run(['sch','export','pdf','-o',pdf,sch])
(ROOT/'validation/export_runs.json').write_text(json.dumps(runs,indent=2),encoding='utf-8')
