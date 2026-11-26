#include "BootOutcome.h"
#include "Observer.h"

#include <string.h>

extern "C" BOOL __cdecl Init();

namespace
{
using namespace MyDll::Boot;
const DWORD PinModule = 0x01;
const DWORD ModuleFromAddress = 0x04;
const DWORD IndirectCallSize = 6;
HMODULE s_module;
BootSession s_boot;

DWORD MainImageSize(DWORD base)
{
    MEMORY_BASIC_INFORMATION page;
    if (!VirtualQuery(reinterpret_cast<const void*>(base), &page, sizeof(page)) ||
        reinterpret_cast<DWORD>(page.AllocationBase) != base || reinterpret_cast<DWORD>(page.BaseAddress) != base ||
        page.State != MEM_COMMIT || page.Type != MEM_IMAGE || page.RegionSize < sizeof(IMAGE_DOS_HEADER) ||
        (page.Protect & PAGE_GUARD))
        return 0;
    const DWORD access = page.Protect & 0xff;
    if (access != PAGE_READONLY && access != PAGE_READWRITE && access != PAGE_WRITECOPY &&
        access != PAGE_EXECUTE_READ && access != PAGE_EXECUTE_READWRITE && access != PAGE_EXECUTE_WRITECOPY)
        return 0;
    const IMAGE_DOS_HEADER* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER)) ||
        page.RegionSize < sizeof(IMAGE_NT_HEADERS))
        return 0;
    const DWORD headerOffset = static_cast<DWORD>(dos->e_lfanew);
    if (headerOffset > page.RegionSize - sizeof(IMAGE_NT_HEADERS))
        return 0;
    const IMAGE_NT_HEADERS* const nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + headerOffset);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER))
        return 0;
    return nt->OptionalHeader.SizeOfImage;
}

bool __cdecl PinOwnModule()
{
    typedef BOOL(WINAPI * PinProc)(DWORD, LPCSTR, HMODULE*);
    HMODULE const kernel = GetModuleHandleA("KERNEL32.dll");
    if (!kernel)
        return false;
    FARPROC const address = GetProcAddress(kernel, "GetModuleHandleExA");
    if (!address)
        return false;
    PinProc pin;
    memcpy(&pin, &address, sizeof(pin));
    HMODULE pinned = NULL;
    return pin(PinModule | ModuleFromAddress, reinterpret_cast<LPCSTR>(s_module), &pinned) && pinned == s_module;
}

bool __cdecl PrepareBoot()
{
    if (!PinOwnModule())
        return false;
    ConfigureObserver(s_boot);
    return true;
}

BOOL __cdecl RunBoot(DWORD returnAddress)
{
    const HostLayout& layout = GameServer102iLayout();
    const DWORD imageBase = reinterpret_cast<DWORD>(GetModuleHandleA(NULL));
    if (imageBase != layout.imageBase || returnAddress != layout.initCall + IndirectCallSize || !s_module)
        return FALSE;
    const BootInvocation invocation = {imageBase, MainImageSize(imageBase), returnAddress,
                                       reinterpret_cast<DWORD>(GetProcAddress(s_module, "Init"))};
    if (!s_boot.backing)
        s_boot.backing = GameServer102iBacking(s_boot.backingSize);
    BootFailure failure;
    const BootResult result = InstallBoot(layout, invocation, s_boot, &PrepareBoot, failure);
    return CompleteBoot(result, failure, layout.relocationCount);
}
} // namespace

extern "C" __declspec(naked) BOOL __cdecl Init()
{
    __asm {
        push dword ptr [esp]
        call RunBoot
        add esp, 4
        ret
    }
}

extern "C" BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        s_module = module;
    return TRUE;
}
