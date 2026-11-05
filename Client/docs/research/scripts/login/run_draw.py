import os
import sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from draw_emu import *
e=DrawEmu()
# dragon effect
e.load_model(0xb4,os.path.join(os.environ.get('MURET_CLIENT_DATA', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 'assets', 'Data')), 'object56', 'dragon.bmd'))
owner=e.alloc(0x1d4)
pos=e.alloc(12); e.wfv(pos,[5600.0,23000.0,0.0]); ang=e.alloc(12); e.wfv(ang,[0,0,0]); light=e.alloc(12); e.wfv(light,[1,1,1])
del e.hooks_at[0x518cf0]
e.hooks_at[0x518cf0]=lambda:None
e.call(0x518cf0,[0xb4,pos,ang,light,0,owner,0xffffffff,0,0,0,e.fbits(1.6)])
fx=0x7491fd8
for i in range(3): e.call(0x5208a0,[fx,0])
e.log=[]; e.events=[]
e.call(0x602e90,[fx,0,0,0])
print('dragon effect draw:')
for l in e.log: print('  ',l)
for ev in e.events: print('  ',ev)
