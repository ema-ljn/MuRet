#pragma once

#include <windows.h>

#if !defined(_MSC_VER) || _MSC_VER != 1200 || !defined(_M_IX86)
#error MyDll requires Visual C++ 6.0 targeting x86.
#endif

namespace MyDll
{
namespace Boot
{
int __cdecl AreaStub();
void __stdcall DiscardCharacterList(DWORD);
int __stdcall RejectSetCharacter(DWORD, DWORD);
DWORD __cdecl DiscardReceive(DWORD, DWORD, DWORD, int, DWORD, DWORD);
int __stdcall NoDuel(DWORD);
DWORD __cdecl DiscardSend(int, char*, DWORD);
void __stdcall LeaveBytesUnchanged(unsigned char*, int);
} // namespace Boot
} // namespace MyDll
