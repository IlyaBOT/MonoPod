"""Isolated native test of EXACT firmware 0.10.6 Theme.cpp; no firmware edits."""
from pathlib import Path
import hashlib, json, shutil, struct, subprocess, sys, zlib

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[2]
FIRMWARE = PROJECT.parent / 'MonoPod_v0.10.6' / 'DIY_iPod_Mono'
ADAPTER = PROJECT.parent / 'MonoPod_v0.10.0' / 'tests' / 'stubs'
VCVARS = Path(r'D:\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat')
for name in ['source', 'stubs', 'fixtures', 'build']:
    (HERE/name).mkdir(exist_ok=True)
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
sources = {}
for name in ['Theme.cpp', 'Theme.h']:
    original = FIRMWARE / name
    copied = HERE / 'source' / name
    shutil.copy2(original, copied)
    assert sha(original) == sha(copied)
    sources[name] = {'source': str(original), 'sha256': sha(original), 'copied_exactly': True}
for name in ['Arduino.h', 'SD.h', 'esp_heap_caps.h']:
    shutil.copy2(ADAPTER/name, HERE/'stubs'/name)
fixture_info = {}
for name in ['csharp-mpt1.mpt', 'csharp-mpt2.mpt']:
    original = PROJECT/'docs'/'theme-fixtures'/name
    shutil.copy2(original, HERE/'fixtures'/name)
    fixture_info[name] = {'source': str(original), 'sha256': sha(original), 'bytes': original.stat().st_size}
compile_file = HERE/'compile.cmd'
compile_file.write_text(f'''@echo off
call "{VCVARS}" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /utf-8 /I"{HERE/'stubs'}" /I"{HERE/'source'}" "{HERE/'source'/'Theme.cpp'}" "{HERE/'theme_contract_test.cpp'}" /Fe:"{HERE/'build'/'theme_contract_test.exe'}" /Fo"{HERE/'build'}/"
''', encoding='utf-8')
build = subprocess.run(['cmd', '/d', '/c', str(compile_file)], cwd=HERE, capture_output=True, text=True, errors='replace')
(HERE/'build.log').write_text(build.stdout + build.stderr, encoding='utf-8')
if build.returncode:
    print(build.stdout + build.stderr)
    raise SystemExit(build.returncode)
exe = HERE/'build'/'theme_contract_test.exe'
results = []
def run(label, *args):
    command = [str(exe), *map(str, args)]
    result = subprocess.run(command, cwd=HERE, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=20)
    results.append({'case': label, 'command': command, 'exit_code': result.returncode, 'output': result.stdout + result.stderr})
    print(label + ': ' + (result.stdout + result.stderr).strip())
for name in fixture_info:
    run('load ' + name, HERE/'fixtures'/name)
valid = HERE/'fixtures'/'csharp-mpt2.mpt'
raw = valid.read_bytes()
for kind in ['truncated', 'crc', 'glyph-width', 'surrogate', 'version', 'glyph-count', 'trailing-payload']:
    bad = bytearray(raw)
    if kind == 'truncated': bad = bad[:43]
    elif kind == 'crc': bad[-1] ^= 255
    elif kind == 'glyph-width': bad[48] = 25
    elif kind == 'surrogate': struct.pack_into('<I', bad, 44, 0xd800)
    elif kind == 'version': bad[4] = 99
    elif kind == 'glyph-count': struct.pack_into('<H', bad, 8, 257)
    elif kind == 'trailing-payload':
        bad += b'\x00'
        struct.pack_into('<I', bad, 36, len(bad)-44)
    if kind in ['glyph-width', 'surrogate', 'trailing-payload']:
        struct.pack_into('<I', bad, 40, zlib.crc32(bad[44:]))
    path = HERE/'fixtures'/('bad-' + kind + '.mpt')
    path.write_bytes(bad)
    run('reject ' + kind + ', retain active theme', valid, path)
report = {'firmware': '0.10.6', 'sources': sources, 'fixtures': fixture_info,
          'adapter_source': str(ADAPTER), 'compiler': str(VCVARS),
          'scope': 'Native host execution of actual Theme.cpp/.h; filesystem and allocation adapters. Not ESP32 hardware execution.',
          'results': results, 'passed': sum(x['exit_code'] == 0 for x in results), 'failed': sum(x['exit_code'] != 0 for x in results)}
(HERE/'native-theme-0106-results.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
(HERE/'run.log').write_text('\n'.join(x['case'] + ': ' + x['output'].strip() for x in results) + '\n', encoding='utf-8')
print(f"{report['passed']} passed / {report['failed']} failed; report: {HERE/'native-theme-0106-results.json'}")
raise SystemExit(bool(report['failed']))
