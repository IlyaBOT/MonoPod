from pathlib import Path
import sys,struct,json,zlib,subprocess
P=Path(__file__).resolve().parents[1];S=P/'Legacy'/'Studio';sys.path.insert(0,str(S));import theme_format as ref
D=P/'docs'/'theme-fixtures';D.mkdir(exist_ok=True)
raw=(D/'csharp-mpt1.mpt').read_bytes();parsed=ref.verify_theme(raw)
fields=ref.HEADER.unpack_from(raw);mc,sc=fields[6:8] if False else (fields[5],fields[6])
pos=28;tables=[]
for count,height in [(mc,16),(sc,12)]:
 start=pos
 for _ in range(count):
  cp,w,adv,res=ref.GLYPH.unpack_from(raw,pos);pos+=7+(w*height+1)//2
 tables.append(raw[start:pos])
assert pos==len(raw)
def hex565(c):
 r=(c>>11)&31;g=(c>>5)&63;b=c&31
 return '#%02x%02x%02x'%((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2))
opts=ref.ThemeOptions(font_path='C:/Windows/Fonts/arial.ttf',main_size=15,small_size=11,bg=hex565(fields[7]),fg=hex565(fields[8]),selected=hex565(fields[9]),muted=hex565(fields[10]))
# Both encoders receive the exact same palette and glyph records.
original=ref.glyph_table;ref.glyph_table=lambda path,size,height:tables[0 if height==16 else 1]
packed=ref.encode_theme(opts);assert packed==raw
ref.glyph_table=original
python=ref.encode_theme(opts);ref.verify_theme(python);(D/'python-reference.mpt').write_bytes(python)
assert len(python)>28
# Compare native rasterizers on the same installed font, sizes, glyph set and palette.
def unpack(data):
 h=ref.HEADER.unpack_from(data);pos=28;result=[]
 for count,height in [(h[5],16),(h[6],12)]:
  glyphs={}
  for _ in range(count):
   cp,w,a,_=ref.GLYPH.unpack_from(data,pos);pos+=7;n=(w*height+1)//2;glyphs[cp]=(w,a,data[pos:pos+n]);pos+=n
  result.append(glyphs)
 return result
a,b=unpack(raw),unpack(python)
assert all(set(x)==set(y)for x,y in zip(a,b))
different=sum(x[cp]!=y[cp]for x,y in zip(a,b)for cp in x)
report=dict(python_verifies_csharp_mpt1=parsed,identical_input_glyphs_and_palette_encoding_byte_exact=True,same_font_options_glyph_sets_match=True,rasterized_glyph_records_differ=different,rasterization_note='GDI+ 4x coverage and Pillow FreeType are different rasterizers. Their glyph pixels are not promised byte-identical. Device and Desktop preview consume the same exported records.',python_reference_bytes=len(python),csharp_mpt1_bytes=len(raw))
native=P/'work'/'native'/'theme_test.exe'
if native.exists():
 r=subprocess.run([str(native),str(D/'python-reference.mpt')],capture_output=True);assert r.returncode==0;report['actual_cpp_loader_accepts_python_reference']=True
(P/'docs'/'theme-crosscheck.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(report,ensure_ascii=False,indent=2))
