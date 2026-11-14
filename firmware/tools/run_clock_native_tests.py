"""Compile and run actual firmware clock validators on the host; no device I/O."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--build-dir', type=Path, default=root / 'work/clock-native')
args = parser.parse_args()
work = args.build_dir.resolve()
work.mkdir(parents=True, exist_ok=True)
source = root / 'tests/clock_config_validation_test.cpp'
stage = work / 'host-source'
stage.mkdir(exist_ok=True)
for stub in (root / 'tests/clock_stubs').glob('*.h'):
    shutil.copy2(stub, stage / stub.name)
for name in ['ClockService.h', 'ClockConfigValidation.h']:
    shutil.copy2(root / 'DIY_iPod_Mono' / name, stage / name)
# Only the platform include is adapted; the actual service implementation is unchanged.
implementation = (root / 'DIY_iPod_Mono/ClockService.cpp').read_text(encoding='utf-8')
assert '#include <sys/time.h>' in implementation
(stage / 'ClockService.cpp').write_text(implementation.replace('settimeofday(&t,nullptr)', 'clock_native_settimeofday(&t,nullptr)').replace('#include <sys/time.h>', '#include "ClockNativePlatform.h"'), encoding='utf-8')
if os.name == 'nt':
    vswhere = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    install = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
    vcvars = Path(install) / 'VC/Auxiliary/Build/vcvars64.bat'
results=[]
for name, sources, include in [
    ('clock_validation', [source], root / 'DIY_iPod_Mono'),
    ('clock_service', [root / 'tests/clock_service_test.cpp', stage / 'ClockService.cpp'], stage),
    ('wheel_arduino_macros', [root / 'tests/wheel_arduino_macro_test.cpp'], root / 'DIY_iPod_Mono'),
]:
    executable=work / (name + ('.exe' if os.name=='nt' else ''))
    if os.name=='nt':
        batch=work / (name + '-compile.cmd')
        quoted=' '.join(f'"{item}"' for item in sources)
        batch.write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\n'
            f'cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /I"{include}" {quoted} '
            f'/Fe:"{executable}" /Fo"{work.as_posix()}/"\n', encoding='utf-8')
        command=['cmd','/c',str(batch)]
    else:
        command=['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(include),*[str(item) for item in sources],'-o',str(executable)]
    build=subprocess.run(command,capture_output=True,text=True,errors='replace',cwd=work)
    (work / (name+'-compile.log')).write_text(build.stdout+build.stderr,encoding='utf-8')
    if build.returncode:
        print(build.stdout+build.stderr);raise SystemExit(build.returncode)
    run=subprocess.run([str(executable)],capture_output=True,text=True,errors='replace',cwd=work)
    results.append({'name':name,'exit_code':run.returncode,'output':run.stdout+run.stderr})
    print(run.stdout+run.stderr)
result = {'exit_code': max(item['exit_code'] for item in results), 'checks':results, 'hardware_io': False,
    'header_sha256': hashlib.sha256((root / 'DIY_iPod_Mono/ClockConfigValidation.h').read_bytes()).hexdigest(),
    'service_sha256':hashlib.sha256(implementation.encode('utf-8')).hexdigest(),
    'service_host_adapter':'sys/time.h replaced by ClockNativePlatform.h; NVS, Wi-Fi, SNTP and OS time are stubbed.'}
(work / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
raise SystemExit(result['exit_code'])
