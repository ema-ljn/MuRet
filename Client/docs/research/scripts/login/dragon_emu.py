import os
import struct, json, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import *
e=Emu()
e.w32(0x6c9dd0,0x37); e.w32(0x6d5dc8,2)
e.set_model(0xb4,[(41,0)],meshes=3,bones=93)
owner=e.alloc(0x1d4)
pos=e.alloc(12); e.wfv(pos,[5600.0,23000.0,0.0])
ang=e.alloc(12); e.wfv(ang,[0,0,0])
light=e.alloc(12); e.wfv(light,[1,1,1])
e.call(0x518cf0,[0xb4,pos,ang,light,0,owner,0xffffffff,0,0,0,e.fbits(1.6)])
fx=0x7491fd8
def dump():
    return dict(alive=e.r8(fx),type=e.r16(fx+2),scale=e.rf(fx+0xc),pos=e.rfv(fx+0x10,3),ang=e.rfv(fx+0x1c,3),
                life=e.ri32(fx+0x60),vel=e.rfv(fx+0xcc,3),speed=e.rf(fx+0xd8),dc=e.rf(fx+0xdc),e4=e.rf(fx+0xe4),ec=e.rf(fx+0xec),
                state=e.r8(fx+0x88),f84=e.rf(fx+0x84),frame=e.rf(fx+0x114),prev=e.rf(fx+0x118),action=e.r8(fx+0x111),
                light=e.rfv(fx+0xf4,3),alpha=e.rf(fx+0x174),m58=e.ri32(fx+0x58),m64=e.ri32(fx+0x64),m68=e.rf(fx+0x68),e8=e.r8(fx+0xe8),start=e.rfv(fx+0x17c,3),owner=hex(e.r32(fx+0x108)))
init=dump()
print(json.dumps(init))
N=int(sys.argv[1]) if len(sys.argv)>1 else 2000
traj=[]
for i in range(N):
    e.call(0x5208a0,[fx,0])
    d=dump(); traj.append(d)
json.dump(dict(init=init,traj=traj,rand=e.rand_log[:20],nrand=len(e.rand_log)),open('dragon_traj.json','w'))
for i in list(range(0,30))+list(range(30,N,25)):
    d=traj[i]; print(i+1, ['%.1f'%x for x in d['pos']], ['%.2f'%x for x in d['ang']], d['state'], '%.1f'%d['f84'], d['e4'], d['dc'], d['ec'], '%.2f'%d['frame'], d['action'], d['life'], d['alive'])
