import os
import sys, struct, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import *
from bmd import load_bmd
from world import find
from unicorn.x86_const import *
def f(b): return struct.unpack('<f',struct.pack('<I',b&0xffffffff))[0]
class DrawEmu(Emu):
    def __init__(self):
        super().__init__()
        self.w32(0x6c9dd0,0x37); self.w32(0x6d5dc8,2)
        self.terrain_light=[0.7,0.67,0.65]
        self.log=[]
        e=self
        def model_color(ecx):
            return [round(x,4) for x in e.rfv(ecx+0x48,3)]
        def thiscall_rec(name,n,fmt):
            def h():
                esp=e.uc.reg_read(UC_X86_REG_ESP); ecx=e.uc.reg_read(UC_X86_REG_ECX)
                a=[e.r32(esp+4+4*k) for k in range(n)]
                mid=(ecx-MODELS_BASE)//MODEL_SIZE if ecx>=MODELS_BASE else None
                args=[]
                for k,c in enumerate(fmt):
                    args.append(round(f(a[k]),4) if c=='f' else (a[k]-(1<<32) if a[k]>=1<<31 else a[k]))
                e.log.append((name,mid,args,model_color(ecx) if mid is not None else None, e.r8(ecx+0x88) if mid is not None else None, e.r8(ecx+0x44) if mid is not None else None))
                e.ret(0,n)
            return h
        # RenderBody(flags, alpha, blendMesh, blendLight, U, V, hiddenMesh, forcedTexture)
        self.at(0x4ef0c0,thiscall_rec('RenderBody',8,'ififffii'))
        # 0x4ecc30(mesh, flags, alpha, blendMesh, light, U, V, texture)
        self.at(0x4ecc30,thiscall_rec('RenderMesh',8,'iififfii'))
        self.at(0x4eff40,thiscall_rec('Shadow',4,'iiii'))
        self.at(0x4ee770,thiscall_rec('0x4ee770',5,'iiiii'))
        def skip(n,name=None):
            def h():
                if name: 
                    esp=e.uc.reg_read(UC_X86_REG_ESP)
                    e.log.append((name,[e.r32(esp+4+4*k) for k in range(n if n else 6)]))
                e.ret(0,n)
            return h
        self.at(0x4eb860,skip(8))     # pose
        self.at(0x4ebc80,skip(5))     # vertices
        self.at(0x60cd60,skip(0,'outline_0x60cd60'))
        def light_at():
            esp=e.uc.reg_read(UC_X86_REG_ESP); out=e.r32(esp+12)
            e.wfv(out,self.terrain_light); e.ret(0)
        self.at(0x5f99e0,light_at)
        for a,name in [(0x5491e0,'sprite_0x5491e0'),(0x53fe70,'particle_0x53fe70'),(0x518cf0,'effect_0x518cf0'),(0x41ed10,'sound_0x41ed10'),(0x5f9780,'dynlight_0x5f9780'),(0x530860,'0x530860'),(0x4ec110,'0x4ec110')]:
            self.at(a,skip(0,name))
    def load_model(self,mid,path):
        m=load_bmd(path)
        self.set_model(mid,m['actions'] and [(nk,0) for nk,_ in m['actions']] or [(1,0)],meshes=len(m['meshes']),bones=len(m['bones']))
        # per-mesh texture index at mesh array (+0x28 ptr, 0x28 bytes each, texture short at +2?) — give a mesh array so 0x4ecc30-like code can read counts
        base=MODELS_BASE+mid*MODEL_SIZE
        arr=self.alloc(0x28*max(1,len(m['meshes'])))
        for i,me in enumerate(m['meshes']):
            self.w16(arr+i*0x28+0,me['nv']); self.w16(arr+i*0x28+2,me['tex']) 
        self.w32(base+0x28,arr)
        tex=self.alloc(2*max(1,len(m['meshes'])))
        self.w32(base+0x38,tex)
        return m
    def gl_events(self):
        return [ev for ev in self.events]
