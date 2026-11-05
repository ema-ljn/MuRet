import struct, sys
from world import k16
def load_bmd(path):
    raw=open(path,'rb').read()
    ver=raw[3]
    if ver==0x0C:
        size=struct.unpack_from('<I',raw,4)[0]
        body=k16(raw[8:8+size])
    else:
        body=raw[4:]
    o=0
    name=body[o:o+32].split(b'\0')[0].decode('latin1'); o+=32
    nm,nb,na=struct.unpack_from('<3h',body,o); o+=6
    meshes=[]
    for m in range(nm):
        nv,nn,nuv,nt,tex=struct.unpack_from('<5h',body,o); o+=10
        verts=[]
        for i in range(nv):
            b,_=struct.unpack_from('<hh',body,o); p=struct.unpack_from('<3f',body,o+4); verts.append((b,p)); o+=16
        o+=20*nn
        uvs=[struct.unpack_from('<2f',body,o+8*i) for i in range(nuv)]; o+=8*nuv
        tris=[]
        for i in range(nt):
            vi=struct.unpack_from('<4h',body,o+2); ui=struct.unpack_from('<4h',body,o+18); tris.append((vi,ui)); o+=64
        tname=body[o:o+32].split(b'\0')[0].decode('latin1'); o+=32
        meshes.append(dict(nv=nv,nn=nn,nuv=nuv,nt=nt,tex=tex,texname=tname,verts=verts,tris=tris))
    actions=[]
    for a in range(na):
        nk=struct.unpack_from('<h',body,o)[0]; lock=body[o+2]; o+=3
        if lock: o+=12*nk
        actions.append((nk,lock))
    bones=[]
    for b in range(nb):
        dummy=body[o]; o+=1
        if dummy: bones.append(None); continue
        bname=body[o:o+32].split(b'\0')[0].decode('latin1'); o+=32
        parent=struct.unpack_from('<h',body,o)[0]; o+=2
        keys=[]
        for (nk,lock) in actions:
            pos=[struct.unpack_from('<3f',body,o+12*i) for i in range(nk)]; o+=12*nk
            rot=[struct.unpack_from('<3f',body,o+12*i) for i in range(nk)]; o+=12*nk
            keys.append((pos,rot))
        bones.append(dict(name=bname,parent=parent,keys=keys))
    assert o==len(body), (o,len(body))
    return dict(ver=ver,name=name,meshes=meshes,actions=actions,bones=bones)
if __name__=='__main__':
    m=load_bmd(sys.argv[1])
    print('ver',hex(m['ver']),'name',m['name'],'meshes',len(m['meshes']),'bones',len(m['bones']),'actions',m['actions'])
    for i,me in enumerate(m['meshes']):
        xs=[v[1] for v in me['verts']]
        mn=[min(p[k] for p in xs) for k in range(3)] if xs else None
        mx=[max(p[k] for p in xs) for k in range(3)] if xs else None
        print(i,me['texname'],'v',me['nv'],'t',me['nt'],'tex',me['tex'],'min',mn,'max',mx)
