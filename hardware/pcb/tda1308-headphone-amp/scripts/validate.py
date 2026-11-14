from pathlib import Path
import subprocess,shutil,json
ROOT=Path(__file__).resolve().parents[1]
NAME='TDA1308_HeadphoneAmp'
CLI=shutil.which('kicad-cli') or r'C:\Program Files\KiCad\10.0\bin\kicad-cli.exe'
runs=[]
def run(args):
    r=subprocess.run([CLI]+[str(x) for x in args],cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace')
    runs.append(dict(command=[CLI]+[str(x) for x in args],returncode=r.returncode,stdout=r.stdout,stderr=r.stderr))
    print(r.stdout.strip());assert r.returncode==0,r.stderr
for ext in ['rpt','json']:
    opts=['--format','json'] if ext=='json' else []
    run(['sch','erc','--severity-all','--exit-code-violations',*opts,'-o',ROOT/f'validation/ERC.{ext}',ROOT/f'{NAME}.kicad_sch'])
    run(['pcb','drc','--schematic-parity','--all-track-errors','--severity-all','--exit-code-violations',*opts,'-o',ROOT/f'validation/DRC.{ext}',ROOT/f'{NAME}.kicad_pcb'])
run(['sch','export','netlist','--format','kicadxml','-o',ROOT/'validation/netlist.xml',ROOT/f'{NAME}.kicad_sch'])
(ROOT/'validation/tool_runs.json').write_text(json.dumps(runs,indent=2),encoding='utf-8')

