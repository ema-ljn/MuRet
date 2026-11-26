#include "BootHooks.h"

#include <stdio.h>
#include <string.h>

namespace
{
const DWORD ShortReturn = 0x00415b7e;
const DWORD LongReturn = 0x00427975;
const DWORD SavedEbx = 0x71727374;
const DWORD SavedEsi = 0x81828384;
const DWORD SavedEdi = 0x91929394;

#pragma data_seg(".landing")
unsigned char s_landingSpace[0x30000] = {1};
#pragma data_seg()

bool SetLanding(DWORD address, DWORD target)
{
    const DWORD base = reinterpret_cast<DWORD>(s_landingSpace);
    if (address < base || address - base > sizeof(s_landingSpace) - 5)
    {
        printf("Fixture landing %08lx outside owned section %08lx\n", address, base);
        return false;
    }
    unsigned char* code = s_landingSpace + (address - base);
    const DWORD displacement = target - (address + 5);
    code[0] = 0xe9;
    memcpy(code + 1, &displacement, sizeof(displacement));
    return FlushInstructionCache(GetCurrentProcess(), code, 5) != FALSE;
}

bool CheckLongFrame()
{
    DWORD tailAddress;
    DWORD outerStack;
    DWORD savedStack;
    DWORD beforePops;
    DWORD restoredEbx;
    DWORD restoredEsi;
    DWORD restoredEdi;
    int result;
    int(__cdecl* const hook)() = &MyDll::Boot::AreaStub;
    __asm {
        mov eax, offset longTail
        mov tailAddress, eax
    }
    if (!SetLanding(LongReturn, tailAddress)) return false;

    __asm {
        push ebx
        push esi
        push edi
        mov outerStack, esp
        push SavedEbx
        push SavedEsi
        push SavedEdi
        mov savedStack, esp
        push 5
        push 4
        push 3
        push 2
        push 1
        push LongReturn
        jmp dword ptr [hook]
    longTail:
        mov result, eax
        add esp, 16
        mov beforePops, esp
        pop edi
        pop esi
        pop ebx
        mov restoredEdi, edi
        mov restoredEsi, esi
        mov restoredEbx, ebx
        mov esp, outerStack
        pop edi
        pop esi
        pop ebx
    }
    const bool correct = beforePops == savedStack && result == 0 && restoredEbx == SavedEbx &&
                         restoredEsi == SavedEsi && restoredEdi == SavedEdi;
    if (!correct)
        printf("Before pops %08lx; expected %08lx; EAX %d; EBX/ESI/EDI %08lx/%08lx/%08lx\n", beforePops, savedStack,
               result, restoredEbx, restoredEsi, restoredEdi);
    return correct;
}

bool CheckShortFrame()
{
    DWORD tailAddress;
    DWORD before;
    DWORD after;
    int result;
    int(__cdecl* const hook)() = &MyDll::Boot::AreaStub;
    __asm {
        mov eax, offset shortTail
        mov tailAddress, eax
    }
    if (!SetLanding(ShortReturn, tailAddress)) return false;

    __asm {
        mov before, esp
        push 2
        push 1
        push ShortReturn
        jmp dword ptr [hook]
    shortTail:
        mov after, esp
        mov result, eax
        mov esp, before
    }
    return before == after && result == 0;
}

bool CheckStackPurge(DWORD hook, DWORD words, DWORD bytesRemoved)
{
    DWORD before;
    DWORD after;
    __asm {
        mov before, esp
        mov ecx, words
    arguments:
        push 0
        loop arguments
        call dword ptr [hook]
        mov after, esp
        mov esp, before
    }
    return after == before - words * sizeof(DWORD) + bytesRemoved;
}

bool CheckCallbackConventions()
{
    using namespace MyDll::Boot;
    return CheckStackPurge(reinterpret_cast<DWORD>(&DiscardCharacterList), 1, 4) &&
           CheckStackPurge(reinterpret_cast<DWORD>(&RejectSetCharacter), 2, 8) &&
           CheckStackPurge(reinterpret_cast<DWORD>(&DiscardReceive), 6, 0) &&
           CheckStackPurge(reinterpret_cast<DWORD>(&NoDuel), 1, 4) &&
           CheckStackPurge(reinterpret_cast<DWORD>(&DiscardSend), 3, 0) &&
           CheckStackPurge(reinterpret_cast<DWORD>(&LeaveBytesUnchanged), 2, 8);
}

bool CheckInertCallbacks()
{
    using namespace MyDll::Boot;
    const unsigned char expected[] = {0xc1, 3, 0xf1, 0xaa};
    unsigned char bytes[sizeof(expected)];
    memcpy(bytes, expected, sizeof(bytes));
    LeaveBytesUnchanged(bytes, static_cast<int>(sizeof(bytes)));
    const bool unchanged = memcmp(bytes, expected, sizeof(bytes)) == 0;

    void* const inaccessible = VirtualAlloc(NULL, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    if (!inaccessible)
        return false;
    const DWORD address = reinterpret_cast<DWORD>(inaccessible);
    DiscardCharacterList(address);
    const int character = RejectSetCharacter(address, address);
    const DWORD rx = DiscardReceive(0, address, 64, 0, 0, 0);
    const int duel = NoDuel(address);
    const DWORD tx = DiscardSend(0, static_cast<char*>(inaccessible), 64);
    LeaveBytesUnchanged(static_cast<unsigned char*>(inaccessible), 64);
    const bool released = VirtualFree(inaccessible, 0, MEM_RELEASE) != FALSE;
    return unchanged && character == 0 && rx == 0x80004005UL && duel == 0 && tx == 1 && released;
}
} // namespace

#ifdef MYDLL_RED_HOOKS
int __cdecl MyDll::Boot::AreaStub()
{
    return 0;
}
void __stdcall MyDll::Boot::DiscardCharacterList(DWORD) {}
int __stdcall MyDll::Boot::RejectSetCharacter(DWORD, DWORD)
{
    return 0;
}
DWORD __cdecl MyDll::Boot::DiscardReceive(DWORD, DWORD, DWORD, int, DWORD, DWORD)
{
    return 0x80004005UL;
}
int __stdcall MyDll::Boot::NoDuel(DWORD)
{
    return 0;
}
DWORD __cdecl MyDll::Boot::DiscardSend(int, char*, DWORD)
{
    return 1;
}
void __stdcall MyDll::Boot::LeaveBytesUnchanged(unsigned char*, int) {}
#endif

int main()
{
    if (!CheckLongFrame())
    {
        puts("FAIL f08 five-DWORD register and stack restoration");
        return 1;
    }
    if (!CheckShortFrame() || !CheckInertCallbacks())
    {
        puts("FAIL f08 two-DWORD frame or inert callbacks");
        return 1;
    }
    if (!CheckCallbackConventions())
    {
        puts("FAIL non-area callback stack conventions");
        return 1;
    }
    puts("PASS boot hooks");
    return 0;
}
