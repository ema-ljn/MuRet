import os
import sys; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from world import *
from cam import *
from bmd import load_bmd
from pose import *
import numpy as np, collections, os
hdr,objs=load_obj()
cache={}
def model(t):
    if t in cache: return cache[t]
    p=find('object56','Object%02d.bmd'%(t+1)) if t+1<10 else find('object56','Object%d.bmd'%(t+1))
    cache[t]=load_bmd(p) if p else None
    return cache[t]
res=[]
for o in objs:
    t=o['type']; pos=list(o['pos']); ang=list(o['ang']); sc=o['scale']
    if t in (0x56,0x5a):
        pos=[8600.0,25000.0,5000.0]; ang=[0,0,0]; sc=110.0 if t==0x5a else 60.0
    m=model(t)
    if m is None or not m['meshes']:
        res.append((o['i'],t,'(sin modelo)',None)); continue
    W=world_verts(m,pos,ang,sc)
    allv=np.concatenate(W)
    sx=[];sy=[];dmin=1e9;inside=0
    for v in allv:
        s,e=project(v)
        if s is None: continue
        d=-e[2]; dmin=min(dmin,d)
        if -1<=s[2]<=1 and 0<=s[0]<=640 and 25<=s[1]<=455: inside+=1
        sx.append(s[0]); sy.append(s[1])
    res.append((o['i'],t,pos,ang,sc,inside,len(allv),(min(sx),max(sx),min(sy),max(sy)) if sx else None,dmin))
vis=[r for r in res if r[3] is not None and len(r)>5 and r[5]>0]
print('objetos con vértices en pantalla:',len(vis),'de',len(objs))
c=collections.Counter(r[1] for r in vis); print(sorted(c.items()))
for r in vis:
    i,t,pos,ang,sc,ins,n,bb,d=r
    print(i,t,'Object%02d'%(t+1),['%.0f'%v for v in pos],['%.0f'%v for v in ang],'%.2f'%sc,'%d/%d'%(ins,n),'bb x %.0f..%.0f y %.0f..%.0f'%bb,'d %.0f'%d)
nv=[r for r in res if r[3] is not None and len(r)>5 and r[5]==0]
print('no visibles:',len(nv), sorted(collections.Counter(r[1] for r in nv).items()))
print('sin modelo:', [ (r[0],r[1]) for r in res if r[3] is None])
