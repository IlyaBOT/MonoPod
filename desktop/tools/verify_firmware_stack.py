"""Check real Xtensa ELF prologues, not a host estimate of stack frame size."""
from pathlib import Path
import subprocess,re,json,hashlib,sys

P=Path(__file__).resolve().parents[1]
toolroot=Path.home()/'AppData/Local/Arduino15/packages/esp32/tools/esp-x32'
versions=sorted(toolroot.iterdir(),key=lambda p:p.stat().st_mtime,reverse=True)
toolbin=next(p/'bin' for p in versions if (p/'bin/xtensa-esp32s3-elf-nm.exe').exists())
E=P/'docs'/'boot-regression';E.mkdir(exist_ok=True)
def inspect(label,elf,target,required=False):
    nm=toolbin/f'xtensa-{target}-elf-nm.exe'
    objdump=toolbin/f'xtensa-{target}-elf-objdump.exe'
    symbols=subprocess.check_output([str(nm),'-n','-C',str(elf)],text=True,errors='replace')
    m=re.search(r'^([0-9a-f]+) T MediaLibrary::scanFolder\(String const&, unsigned char\)$',symbols,re.M)
    if not m:raise RuntimeError('scanFolder symbol not found: '+str(elf))
    addr=int(m[1],16)
    code=subprocess.check_output([str(objdump),'-d','-C',f'--start-address={addr}',f'--stop-address={addr+32}',str(elf)],text=True,errors='replace')
    (E/(label+'-scan-disassembly.txt')).write_text(code,encoding='utf-8')
    m=re.search(r'entry\s+a1,\s*(0x[0-9a-f]+|[0-9]+)',code)
    if not m:raise RuntimeError('Unknown Xtensa prologue')
    frame=int(m[1],0);guard_frames=8
    result={'profile':label,'elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'recursive_scan_frame_bytes':frame,'max_scan_depth':6,'conservative_active_scan_frames_including_depth_guard':guard_frames,'scan_frames_total_bytes':frame*guard_frames,'loop_task_default_stack_bytes':8192,'scope':'Binary frame bound only. Driver calls and actual runtime high-water still require hardware.'}
    if required:
        result['passed']=frame<=512 and frame*guard_frames<=4096
        if not result['passed']:raise RuntimeError('Recursive stack frame regression: '+str(result))
    return result

results=[]
for label,folder,target in [('s3','build-s3','esp32s3'),('esp32','build-esp32','esp32')]:
    results.append(inspect(label,P/'work'/folder/'DIY_iPod_Mono.ino.elf',target,True))
cached=Path.home()/'AppData/Local/arduino/sketches'
for label,folder in [('v0.8.9.2-reference','7973E7AD221BC4B9727A4DA17C32301C'),('v0.9.0-crash','1FDC6AE6386A315C1F7B9BA40EB8C704')]:
    elf=cached/folder/'DIY_iPod_Mono.ino.elf'
    if elf.exists():results.append(inspect(label,elf,'esp32s3'))
(E/'stack-verification.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
for r in results:print(r['profile'],r['recursive_scan_frame_bytes'],'bytes per recursive frame',r.get('passed','reference'))
