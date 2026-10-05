"""Unpack a Grim Dawn .arc (v3, LZ4 parts) offline -- e.g. resources/Text_EN.arc for every localized string (tagUse=Interact).
Usage: uv run --with lz4 tools/arc_unpack.py  (edit p / the print below). Left by an RE agent, 2026-08-22."""
import struct, zlib, lz4.block
import os
from gamepath import GAME_DIR
import sys
p = sys.argv[2] if len(sys.argv) > 2 else os.path.join(GAME_DIR, "resources", "Text_EN.arc")   # usage: arc_unpack.py <line-regex> [arc path]
d=open(p,'rb').read()
magic,ver,numE,numP,recSize,strSize,recOff=struct.unpack_from("<IIIIIII",d,0)
parts=[struct.unpack_from("<III",d,recOff+12*i) for i in range(numP)]
strs=d[recOff+recSize:recOff+recSize+strSize]
ent=recOff+recSize+strSize
for i in range(numE):
    typ,off,csz,usz,adler,ft1,ft2,np,pi,slen,so=struct.unpack_from("<11I",d,ent+44*i)
    nm=strs[so:strs.index(b'\0',so)].decode(errors='replace')
    blob=b""
    for j in range(pi,pi+np):
        o,c,u=parts[j]
        chunk=d[o:o+c]
        blob += chunk if c==u else lz4.block.decompress(chunk, uncompressed_size=u)
    print("===",nm,len(blob))
    txt=blob.decode('utf-8-sig',errors='replace')
    import re, sys
    rx = re.compile(sys.argv[1], re.I) if len(sys.argv) > 1 else None   # usage: arc_unpack.py <regex over "tag=text" lines>
    for line in txt.splitlines():
        if rx and rx.search(line): print("   ", line)
