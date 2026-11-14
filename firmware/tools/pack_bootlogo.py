#!/usr/bin/env python3
"""Convert any PNG to a 160x128 RGB565 splash (/MonoPod/boot.rgb565).
Usage: python pack_bootlogo.py logo.png boot.rgb565
Requires Pillow: pip install pillow
"""
import sys,struct
from PIL import Image,ImageOps
if len(sys.argv)!=3:raise SystemExit(__doc__)
im=Image.open(sys.argv[1]).convert("RGBA")
bg=Image.new("RGB",(160,128),(0,0,0))
fit=ImageOps.contain(im,(160,128),Image.Resampling.LANCZOS)
x=(160-fit.width)//2;y=(128-fit.height)//2
bg.paste(fit,(x,y),fit.getchannel('A'))
with open(sys.argv[2],'wb') as out:
 for r,g,b in bg.getdata():out.write(struct.pack('<H',((r>>3)<<11)|((g>>2)<<5)|(b>>3)))
print("Written",sys.argv[2],"(40960 bytes RGB565LE)")
