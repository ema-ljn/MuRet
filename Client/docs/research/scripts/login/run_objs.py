import sys, json, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from draw_emu import *
from world import load_obj, find
hdr,objs=load_obj()
types=sorted(set(o['type'] for o in objs))
out={}
for t in types:
    e=DrawEmu()
    p=find('object56','Object%02d.bmd'%(t+1))
    if p: m=e.load_model(t,p)
    else: m=None
    o=[x for x in objs if x['type']==t][0]
    pos=e.alloc(12); e.wfv(pos,list(o['pos'])); ang=e.alloc(12); e.wfv(ang,list(o['ang']))
    try:
        obj=e.call(0x606660,[t,pos,ang,e.fbits(o['scale'])])
    except Exception as ex:
        print(t,'create error',ex); continue
    st=lambda: dict(scale=round(e.rf(obj+0xc),4),pos=[round(v,1) for v in e.rfv(obj+0x10,3)],ang=[round(v,2) for v in e.rfv(obj+0x1c,3)],
        m58=e.ri32(obj+0x58),blend=e.ri32(obj+0x64),blight=round(e.rf(obj+0x68),3),U=round(e.rf(obj+0x6c),3),V=round(e.rf(obj+0x70),3),
        f74=e.ri32(obj+0x74),f78=hex(e.r32(obj+0x78)),speed=round(e.rf(obj+0xd8),3),e8=e.r8(obj+0xe8),light=[round(v,3) for v in e.rfv(obj+0xf4,3)],
        alpha=round(e.rf(obj+0x174),3),target=round(e.rf(obj+0x170),3),frame=round(e.rf(obj+0x114),3),action=e.r8(obj+0x111))
    s0=st()
    e.w8(obj+0x16c,1)
    errs=None
    try:
        for k in range(3):
            e.call(0x604ab0,[obj])
    except Exception as ex:
        errs='logic error %s'%ex
    s1=st()
    e.log=[]; e.events=[]
    try:
        e.call(0x602e90,[obj,0,0,0])
    except Exception as ex:
        errs=(errs or '')+' draw error %s'%ex
    out[t]=dict(file=os.path.basename(p) if p else None,created=s0,after3=s1,draw=e.log,gl=[ev for ev in e.events if ev[1] not in ('glPushMatrix',)],rand=len(e.rand_log),err=errs)
    print('== type',t,out[t]['file'],'err',errs)
    print('   created',s0)
    if s1!=s0: print('   after 3 steps',{k:v for k,v in s1.items() if s0.get(k)!=v})
    for l in e.log: print('   ',l)
    for ev in e.events: print('   ',ev)
json.dump(out,open('objs_draw.json','w'),indent=1,default=str)
