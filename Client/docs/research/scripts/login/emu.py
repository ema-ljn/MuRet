import os
import struct, pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED, UcError
from unicorn.x86_const import *
EXE=os.environ['MURET_MAIN_EXE']
OP_NEW, OP_FREE, RAND = 0x69f403, 0x69eff0, 0x69fd70
GDT_BASE, TEB_BASE = 0x00010000, 0x00011000
STUB_BASE = 0x00020000
SENTINEL = 0x00030000
STACK_BASE, STACK_SIZE = 0x00100000, 0x00100000
HEAP_BASE, HEAP_SIZE = 0x10000000, 0x04000000
MODELS_BASE=0x0B000000; MODEL_SIZE=0xe0; MODELS_POINTER=0x5769ad8
def page_up(n): return (n+0xfff)&~0xfff
class Emu:
    def __init__(self, stub_ok=None, fpcw=0x27f):
        self.pe=pefile.PE(EXE)
        self.uc=Uc(UC_ARCH_X86,UC_MODE_32)
        self.heap=HEAP_BASE; self.events=[]; self.stubs={}; self.error=None; self.fpcw=fpcw
        self.rand_state=1; self.rand_log=[]
        self.hooks_at={}
        base=self.pe.OPTIONAL_HEADER.ImageBase
        self.uc.mem_map(base,0x1000); self.uc.mem_write(base,bytes(self.pe.header[:0x1000]))
        for s in self.pe.sections:
            st=base+s.VirtualAddress; sz=page_up(max(s.Misc_VirtualSize,s.SizeOfRawData))
            self.uc.mem_map(st,sz); self.uc.mem_write(st,s.get_data()[:max(s.Misc_VirtualSize,s.SizeOfRawData)])
        uc=self.uc
        for b,sz in [(GDT_BASE,0x1000),(TEB_BASE,0x1000),(STUB_BASE,0x4000),(SENTINEL,0x1000),(STACK_BASE,STACK_SIZE),(HEAP_BASE,HEAP_SIZE),(MODELS_BASE,page_up(0x489d*MODEL_SIZE))]:
            uc.mem_map(b,sz)
        uc.mem_write(SENTINEL,b'\xf4'); uc.mem_write(STUB_BASE,b'\xc3'*0x4000)
        uc.mem_write(TEB_BASE,struct.pack('<I',0xffffffff)); uc.mem_write(TEB_BASE+0x18,struct.pack('<I',TEB_BASE))
        def entry(base,limit,access,flags):
            e=limit&0xffff; e|=(base&0xffffff)<<16; e|=(access&0xff)<<40; e|=((limit>>16)&0xf)<<48; e|=(flags&0xf)<<52; e|=((base>>24)&0xff)<<56
            return struct.pack('<Q',e)
        gdt=b'\0'*8+entry(0,0xfffff,0x9a,0xc)+entry(0,0xfffff,0x92,0xc)+entry(TEB_BASE,0xfff,0xf2,0x4)
        uc.mem_write(GDT_BASE,gdt); uc.reg_write(UC_X86_REG_GDTR,(0,GDT_BASE,len(gdt)-1,0))
        self.w32(MODELS_POINTER,MODELS_BASE)
        idx=0
        for dll in self.pe.DIRECTORY_ENTRY_IMPORT:
            for imp in dll.imports:
                name=imp.name.decode() if imp.name else 'ord%d'%imp.ordinal
                stub=STUB_BASE+idx*8; self.stubs[stub]=(dll.dll.decode(),name)
                uc.mem_write(imp.address,struct.pack('<I',stub)); idx+=1
        def on_code(_uc,a,s,d):
            fn=self.hooks_at.get(a)
            if fn: fn()
        self.at(OP_NEW,self._on_new); self.at(OP_FREE,lambda:self.ret(0)); self.at(RAND,self._on_rand)
        uc.hook_add(UC_HOOK_CODE,self._on_stub,begin=STUB_BASE,end=STUB_BASE+0x3fff)
        def unm(_uc,acc,addr,size,val,_d):
            self.error='unmapped %s at %s'%(hex(addr),hex(uc.reg_read(UC_X86_REG_EIP))); return False
        uc.hook_add(UC_HOOK_MEM_UNMAPPED,unm)
    def at(self,addr,fn):
        if addr not in self.hooks_at:
            self.uc.hook_add(UC_HOOK_CODE,lambda _uc,a,s,d,addr=addr:self.hooks_at[addr](),begin=addr,end=addr)
        self.hooks_at[addr]=fn
    GL={'glBegin':'i','glEnd':'','glVertex3f':'fff','glTexCoord2f':'ff','glColor3f':'fff','glColor3fv':'p3','glColor4f':'ffff','glVertex3fv':'p3','glEnable':'i','glDisable':'i','glBlendFunc':'ii','glBindTexture':'ii','glDepthMask':'i','glAlphaFunc':'if','glDepthFunc':'i','glCullFace':'i','glPushMatrix':'','glPopMatrix':'','glColor4fv':'p4','glTexCoord2fv':'p2','glNormal3fv':'p3','glFogf':'if','glFogi':'ii','glFogfv':'ip4','glTexEnvf':'iif','glTexEnvi':'iii','glShadeModel':'i','glLoadIdentity':'','glMatrixMode':'i','glTranslatef':'fff','glRotatef':'ffff','glScalef':'fff','glColor4ub':'iiii'}
    def _on_stub(self,_uc,address,_s,_d):
        dll,name=self.stubs.get(address,('?','?'))
        esp=self.uc.reg_read(UC_X86_REG_ESP)
        if name not in self.GL:
            self.error='import %s!%s'%(dll,name); self.uc.emu_stop(); return
        sig=self.GL[name]; args=[]; off=4; cnt=0; k=0
        while k<len(sig):
            c=sig[k]; raw=self.r32(esp+off)
            if c=='p': n=int(sig[k+1]); args.append(self.rfv(raw,n)); k+=2
            else: args.append(struct.unpack('<f',struct.pack('<I',raw))[0] if c=='f' else raw); k+=1
            off+=4; cnt+=1
        self.events.append(('gl',name,args))
        self.ret(0,cnt)
    def _on_new(self):
        size=self.r32(self.uc.reg_read(UC_X86_REG_ESP)+4); self.ret(self.alloc(size))
    def _on_rand(self):
        r=self.r32(self.uc.reg_read(UC_X86_REG_ESP))
        self.rand_state=(self.rand_state*0x343fd+0x269ec3)&0xffffffff
        v=(self.rand_state>>16)&0x7fff
        self.rand_log.append((hex(r),v)); self.ret(v)
    def alloc(self,size):
        size=max(int(size),1); a=self.heap; self.heap=(self.heap+size+15)&~15
        self.uc.mem_write(a,b'\0'*size); return a
    def ret(self,value=None,pop=0):
        esp=self.uc.reg_read(UC_X86_REG_ESP); r=self.r32(esp)
        if value is not None: self.uc.reg_write(UC_X86_REG_EAX,value&0xffffffff)
        self.uc.reg_write(UC_X86_REG_ESP,esp+4+4*pop); self.uc.reg_write(UC_X86_REG_EIP,r)
    def r32(self,a): return struct.unpack('<I',bytes(self.uc.mem_read(a,4)))[0]
    def ri32(self,a): return struct.unpack('<i',bytes(self.uc.mem_read(a,4)))[0]
    def r16(self,a): return struct.unpack('<h',bytes(self.uc.mem_read(a,2)))[0]
    def r8(self,a): return bytes(self.uc.mem_read(a,1))[0]
    def rf(self,a): return struct.unpack('<f',bytes(self.uc.mem_read(a,4)))[0]
    def rfv(self,a,n): return list(struct.unpack('<%df'%n,bytes(self.uc.mem_read(a,4*n))))
    def w32(self,a,v): self.uc.mem_write(a,struct.pack('<I',v&0xffffffff))
    def w16(self,a,v): self.uc.mem_write(a,struct.pack('<H',v&0xffff))
    def w8(self,a,v): self.uc.mem_write(a,struct.pack('<B',v&0xff))
    def wf(self,a,v): self.uc.mem_write(a,struct.pack('<f',v))
    def wfv(self,a,vs): self.uc.mem_write(a,struct.pack('<%df'%len(vs),*vs))
    def fbits(self,x): return struct.unpack('<I',struct.pack('<f',x))[0]
    def call(self,address,args=(),ecx=0,limit=50_000_000):
        uc=self.uc
        uc.reg_write(UC_X86_REG_FPCW,self.fpcw)
        uc.reg_write(UC_X86_REG_SS,2<<3); uc.reg_write(UC_X86_REG_DS,2<<3); uc.reg_write(UC_X86_REG_ES,2<<3); uc.reg_write(UC_X86_REG_FS,(3<<3)|3)
        esp=STACK_BASE+STACK_SIZE-0x4000
        for v in reversed(args): esp-=4; self.w32(esp,v)
        esp-=4; self.w32(esp,SENTINEL)
        uc.reg_write(UC_X86_REG_ESP,esp); uc.reg_write(UC_X86_REG_EBP,0); uc.reg_write(UC_X86_REG_ECX,ecx)
        self.error=None
        try:
            uc.emu_start(address,SENTINEL,count=limit)
        except UcError as e:
            if self.error is None: self.error='%s at %s'%(e,hex(uc.reg_read(UC_X86_REG_EIP)))
        if self.error: raise RuntimeError(self.error)
        if uc.reg_read(UC_X86_REG_EIP)!=SENTINEL: raise RuntimeError('did not return: eip %s'%hex(uc.reg_read(UC_X86_REG_EIP)))
        return uc.reg_read(UC_X86_REG_EAX)
    def set_model(self,mid,actions,meshes=0,bones=0):
        m=MODELS_BASE+mid*MODEL_SIZE
        self.w16(m+0x26,len(actions)); self.w16(m+0x24,meshes); self.w16(m+0x22,bones)
        arr=self.alloc(0x10*max(1,len(actions)))
        for i,(nk,loop) in enumerate(actions):
            self.w8(arr+i*0x10,loop); self.w16(arr+i*0x10+8,nk); self.w8(arr+i*0x10+0xa,0)
        self.w32(m+0x30,arr)
        return m
