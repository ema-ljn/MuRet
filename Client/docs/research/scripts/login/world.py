import struct, os
DATA=os.environ.get('MURET_CLIENT_DATA', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 'assets', 'Data'))
KEY=bytes([0xD1,0x73,0x52,0xF6,0xD2,0x9A,0xCB,0x27,0x3E,0xAF,0x59,0x31,0x37,0xB3,0xE7,0xA2])
def k16(b):
    out=bytearray(len(b)); prev=0x5E
    for i,c in enumerate(b):
        out[i]=((KEY[i%16]^c)-prev)&0xff
        prev=(c+0x3D)&0xff
    return bytes(out)
def find(folder,name):
    d=os.path.join(DATA,folder)
    for f in os.listdir(d):
        if f.lower()==name.lower(): return os.path.join(d,f)
    return None
def load_obj(world=56):
    p=find('world%d'%world,'EncTerrain%d.obj'%world)
    b=k16(open(p,'rb').read())
    ver,w,n=b[0],b[1],struct.unpack_from('<h',b,2)[0]
    objs=[]
    for i in range(n):
        o=4+30*i
        t=struct.unpack_from('<h',b,o)[0]
        f=struct.unpack_from('<7f',b,o+2)
        objs.append(dict(i=i,type=t,pos=f[0:3],ang=f[3:6],scale=f[6]))
    return (ver,w,n),objs
def load_map(world=56):
    p=find('world%d'%world,'EncTerrain%d.map'%world)
    b=k16(open(p,'rb').read())
    return b[0],b[1],b[2:2+65536],b[2+65536:2+2*65536],b[2+2*65536:2+3*65536]
def load_att(world=56):
    p=find('world%d'%world,'EncTerrain%d.att'%world)
    raw=open(p,'rb').read()
    b=bytearray(k16(raw))
    x3=[0xFC,0xCF,0xAB]
    for i in range(len(b)): b[i]^=x3[i%3]
    return bytes(b)
def load_height(world=56,scale=None):
    p=find('world%d'%world,'TerrainHeight.OZB')
    b=open(p,'rb').read()
    h=b[0x43C:0x43C+65536]
    s=scale if scale is not None else (3.0 if world==56 else 1.5)
    return [x*s for x in h]
