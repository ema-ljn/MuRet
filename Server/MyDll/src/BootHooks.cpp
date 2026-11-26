#include "BootHooks.h"

namespace MyDll
{
namespace Boot
{
namespace
{
const DWORD ShortAreaReturn = 0x00415b7e;
const DWORD LongAreaReturn = 0x00427975;
const DWORD ReceiveClosed = 0x80004005UL;
const DWORD SendConsumed = 1;
} // namespace

__declspec(naked) int __cdecl AreaStub()
{
    __asm {
        xor eax, eax
        mov ecx, dword ptr [esp]
        cmp ecx, ShortAreaReturn
        je shortFrame
        cmp ecx, LongAreaReturn
        je longFrame
        ret
    longFrame:
        ret 4
    shortFrame:
        ret 8
    }
}

void __stdcall DiscardCharacterList(DWORD) {}

int __stdcall RejectSetCharacter(DWORD, DWORD)
{
    return 0;
}

DWORD __cdecl DiscardReceive(DWORD, DWORD, DWORD, int, DWORD, DWORD)
{
    return ReceiveClosed;
}

int __stdcall NoDuel(DWORD)
{
    return 0;
}

DWORD __cdecl DiscardSend(int, char*, DWORD)
{
    return SendConsumed;
}

void __stdcall LeaveBytesUnchanged(unsigned char*, int) {}
} // namespace Boot
} // namespace MyDll
