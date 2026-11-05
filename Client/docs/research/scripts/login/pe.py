import os
import struct, sys
B=os.environ['MURET_MAIN_EXE']
data=open(B,'rb').read()
pe=struct.unpack_from('<I',data,0x3c)[0]
nsec=struct.unpack_from('<H',data,pe+6)[0]
optsz=struct.unpack_from('<H',data,pe+20)[0]
base=struct.unpack_from('<I',data,pe+24+28)[0]
secs=[]
off=pe+24+optsz
for i in range(nsec):
    name=data[off:off+8].rstrip(b'\0').decode()
    vsize,va,rsize,raw=struct.unpack_from('<IIII',data,off+8)
    secs.append((name,va+base,vsize,raw,rsize))
    off+=40
def va2off(va):
    for n,sva,vs,raw,rs in secs:
        if sva<=va<sva+max(vs,rs):
            o=va-sva
            if o>=rs: return None
            return raw+o
    return None
def rd(va,n):
    o=va2off(va)
    if o is None: return b'\0'*n
    return data[o:o+n]
def u32(va): return struct.unpack('<I',rd(va,4))[0]
def f32(va): return struct.unpack('<f',rd(va,4))[0]
def f64(va): return struct.unpack('<d',rd(va,8))[0]
def cstr(va):
    b=rd(va,256); return b.split(b'\0')[0].decode('latin1')
if __name__=='__main__':
    for n in secs: print(n[0],hex(n[1]),hex(n[2]),hex(n[3]),hex(n[4]))
