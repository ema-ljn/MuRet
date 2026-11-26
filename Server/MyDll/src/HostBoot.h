#pragma once

#include "BootHooks.h"

namespace MyDll
{
namespace Boot
{
enum Slot
{
    AreaSlot,
    CharacterListSlot,
    CharacterSlot,
    ReceiveSlot,
    DuelSlot,
    SendSlot,
    WireSlot,
    NativeMapSlot,
    SlotCount
};

struct Signature
{
    DWORD address;
    const unsigned char* bytes;
    DWORD size;
};

struct Relocation
{
    DWORD operandAddress;
    DWORD expectedValue;
    DWORD backingOffset;
};

struct HostLayout
{
    DWORD imageBase;
    DWORD imageSize;
    DWORD initCall;
    DWORD initCell;
    DWORD slots[SlotCount];
    const Signature* signatures;
    unsigned signatureCount;
    const Relocation* relocations;
    unsigned relocationCount;
};

struct BootInvocation
{
    DWORD imageBase;
    DWORD imageSize;
    DWORD returnAddress;
    DWORD initAddress;
};

enum BootResult
{
    ForeignHost,
    Rejected,
    Installed,
    AlreadyInstalled
};
enum BootState
{
    Cold,
    Installing,
    Active,
    Failed
};

typedef DWORD(__cdecl* ReceiveHook)(DWORD, DWORD, DWORD, int, DWORD, DWORD);
typedef DWORD(__cdecl* SendHook)(int, char*, DWORD);

struct BootSession
{
    BootState state;
    unsigned char* backing;
    DWORD backingSize;
    ReceiveHook receive;
    SendHook send;
};

struct BootFailure
{
    const char* stage;
    unsigned row;
    DWORD address;
    DWORD expected;
    DWORD observed;
    bool hasValue;
};

typedef bool(__cdecl* PrepareResources)();
BootResult InstallBoot(const HostLayout&, const BootInvocation&, BootSession&, PrepareResources, BootFailure&);
const HostLayout& GameServer102iLayout();
unsigned char* GameServer102iBacking(DWORD& size);

#ifdef MYDLL_HOST_TEST
typedef BOOL(WINAPI* ProtectMemory)(LPVOID, DWORD, DWORD, PDWORD);
typedef BOOL(WINAPI* FlushCode)(HANDLE, LPCVOID, DWORD);
void SetMemoryHooksForTest(ProtectMemory protect, FlushCode flush);
#endif
} // namespace Boot
} // namespace MyDll
