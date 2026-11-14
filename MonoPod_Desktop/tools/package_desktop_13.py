"""Package the tested local Desktop 1.3.0 build; no device access."""
from pathlib import Path
import hashlib,json,zipfile,xml.etree.ElementTree as ET
r=Path(__file__).resolve().parents[1]
v=r/'docs/validation-1.3.0'; pub=r/'publish/win-x64-1.3.0'; out=r/'outputs';out.mkdir(exist_ok=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,o):p.write_text(json.dumps(o,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
trx=ET.parse(v/'monopoddb.trx');ns={'t':'http://microsoft.com/schemas/VisualStudio/TeamTest/2010'}
c=trx.find('.//t:Counters',ns);assert c is not None and c.attrib['failed']=='0' and c.attrib['passed']=='269',c.attrib
assert 'Started; tabs=5' in (v/'smoke/ui.txt').read_text()
assert (pub/'MonoPod.Desktop.exe').is_file() and (pub/'TagLibSharp.dll').is_file()
backup=r/'work/monopoddb-1.3.0-20261102-230437'
source=[p for folder in ['MonoPod.Core','MonoPod.Desktop','MonoPod.Desktop.Tests'] for p in (r/folder).rglob('*') if p.is_file() and not any(s in ['bin','obj'] for s in p.relative_to(r).parts)]
source += [r/'Directory.Build.props',r/'MonoPod.Desktop.sln',r/'README.md',r/'.gitignore']
source += list((r/'tools').glob('*.py'))+list((r/'tools').glob('*.ps1'))+list((r/'docs').glob('*.md'))
changes=[]
for p in source:
    old=backup/p.relative_to(r)
    rel=p.relative_to(r).as_posix()
    manual={'README.md':'modified','docs/BUILD.md':'modified','docs/RELEASE_1.3_RU.md':'added','docs/THIRD_PARTY_NOTICES.md':'added','tools/package_desktop_13.py':'added'}
    if not old.exists() and p.parts[len(r.parts)] not in ['MonoPod.Core','MonoPod.Desktop','MonoPod.Desktop.Tests'] and rel not in manual:continue
    if not old.exists() or sha(old)!=sha(p):changes.append({'path':p.relative_to(r).as_posix(),'status':'modified' if old.exists() else 'added','sha256':sha(p),'before_sha256':sha(old) if old.exists() else None})
    if rel in manual and changes and changes[-1]['path']==rel:changes[-1]['status']=manual[rel]
write(v/'changed-files.json',{'backup':str(backup),'files':changes})
write(v/'report.json',{'version':'1.3.0','firmware_source_checked':'MonoPod_v0.10.7.1','tests_passed':269,'physical_device_tested':False,
    'smoke':'passed; 5 tabs','audio_tests':'MP3/FLAC tags, picture, comment, custom field, track total, decoded audio SHA256 unchanged',
    'large_transfer':'synthetic 263 MiB; requests <=16 KiB; managed memory growth <64 MiB',
    'device_cache':'guid / OTA hardware ID / ESP native USB MAC serial; session-only when identity unavailable',
    'files':changes,'published_core_sha256':sha(pub/'MonoPod.Core.dll'),'published_desktop_sha256':sha(pub/'MonoPod.Desktop.dll')})
# Include current reports in both archives, and current Markdown in ready-to-run docs.
for p in (r/'docs').glob('*.md'):
    target=pub/'docs'/p.name;target.parent.mkdir(exist_ok=True);target.write_bytes(p.read_bytes())
runtime=sorted(p for p in pub.rglob('*') if p.is_file())
def package(name,items,root,prefix):
    path=out/name
    with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for p in sorted(set(items)):z.write(p,(Path(prefix)/p.relative_to(root)).as_posix())
    with zipfile.ZipFile(path) as z:
        assert z.testzip() is None
        for p in set(items):assert hashlib.sha256(z.read((Path(prefix)/p.relative_to(root)).as_posix())).hexdigest()==sha(p)
    digest=sha(path);path.with_suffix('.zip.sha256').write_text(digest+'  '+path.name+'\n',encoding='utf-8')
    return {'file':str(path),'bytes':path.stat().st_size,'sha256':digest,'entries':len(set(items)),'crc_and_all_file_sha256':'passed'}
reports=[p for p in v.rglob('*') if p.is_file()]
results=[package('MonoPod_Desktop_1.3.0_win-x64.zip',runtime,pub,'MonoPod_Desktop_1.3.0'),package('MonoPod_Desktop_1.3.0_source.zip',source+reports,r,'MonoPod_Desktop')]
write(out/'MonoPod_Desktop_1.3.0_package_validation.json',{'packages':results,'tests':269,'physical_device_tested':False})
print(json.dumps(results,indent=2))
