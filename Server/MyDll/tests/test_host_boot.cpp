#include "HostBoot.h"
#ifndef MYDLL_RED_HOST
#include "BootOutcome.h"
#endif

#include <stdio.h>
#include <string.h>

namespace
{
using namespace MyDll::Boot;

const DWORD PageSize = 0x1000;
const DWORD ImageSize = 4 * PageSize;
const DWORD Canary = 0x63748321;
const DWORD FirstWord = 0x12345678;
const DWORD SecondWord = 0x98765432;
const unsigned char ExpectedInstruction = 0x90;
unsigned s_prepareCount;
bool s_prepareAllowed;
unsigned s_fault;
unsigned s_protectCalls;
DWORD s_conflictSlot;

enum Fault
{
    NoFault,
    SecondProtectFails,
    RestoreFails,
    FlushFails,
    SlotConflict
};

bool __cdecl Prepare()
{
    ++s_prepareCount;
    return s_prepareAllowed;
}

void Store(DWORD address, DWORD value)
{
    memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
}

DWORD Read(DWORD address)
{
    DWORD value;
    memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}

DWORD Protection(DWORD address)
{
    MEMORY_BASIC_INFORMATION page;
    return VirtualQuery(reinterpret_cast<void*>(address), &page, sizeof(page)) ? page.Protect : 0;
}

BOOL WINAPI Protect(LPVOID page, DWORD size, DWORD access, PDWORD previous)
{
    ++s_protectCalls;
    if ((s_fault == SecondProtectFails && s_protectCalls == 4) || (s_fault == RestoreFails && s_protectCalls == 2))
        return FALSE;
    return VirtualProtect(page, size, access, previous);
}

BOOL WINAPI Flush(HANDLE process, LPCVOID code, DWORD size)
{
    if (s_fault == FlushFails)
        return FALSE;
    if (s_fault == SlotConflict)
        Store(s_conflictSlot, Canary);
    return FlushInstructionCache(process, code, size);
}

class Fixture
{
public:
    Fixture() : m_image(VirtualAlloc(NULL, ImageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))
    {
        memset(&layout, 0, sizeof(layout));
        memset(&invocation, 0, sizeof(invocation));
        memset(&failure, 0, sizeof(failure));
        session.state = Cold;
        session.backing = m_backing;
        session.backingSize = sizeof(m_backing);
        s_prepareAllowed = true;
        s_prepareCount = 0;
        s_protectCalls = 0;
        s_fault = NoFault;
        if (!m_image)
            return;

        const DWORD base = reinterpret_cast<DWORD>(m_image);
        layout.imageBase = base;
        layout.imageSize = ImageSize;
        layout.initCall = base + 0x20;
        layout.initCell = base + 0x80;
        layout.relocations = m_relocations;
        layout.relocationCount = 2;
        layout.signatures = &m_signature;
        layout.signatureCount = 1;
        m_signature.address = base + 0x35;
        m_signature.bytes = &ExpectedInstruction;
        m_signature.size = 1;
        m_relocations[0].operandAddress = base + 0x100;
        m_relocations[0].expectedValue = FirstWord;
        m_relocations[0].backingOffset = 4;
        m_relocations[1].operandAddress = base + 2 * PageSize - 2;
        m_relocations[1].expectedValue = SecondWord;
        m_relocations[1].backingOffset = 12;
        invocation.imageBase = base;
        invocation.imageSize = ImageSize;
        invocation.returnAddress = layout.initCall + 6;
        invocation.initAddress = base + 0x140;

        unsigned char* const bytes = static_cast<unsigned char*>(m_image);
        bytes[0x20] = 0xff;
        bytes[0x21] = 0x15;
        bytes[0x35] = ExpectedInstruction;
        Store(layout.initCall + 2, layout.initCell);
        Store(layout.initCell, invocation.initAddress);
        Store(m_relocations[0].operandAddress, FirstWord);
        Store(m_relocations[1].operandAddress, SecondWord);
        for (unsigned i = 0; i < SlotCount; ++i)
        {
            layout.slots[i] = base + 3 * PageSize + 8 * i;
            Store(layout.slots[i] + sizeof(DWORD), Canary);
        }
        s_conflictSlot = layout.slots[AreaSlot];
    }

    ~Fixture()
    {
        if (m_image)
            VirtualFree(m_image, 0, MEM_RELEASE);
    }

    bool Seal() const
    {
        if (!m_image)
            return false;
        DWORD previous;
        unsigned char* const base = static_cast<unsigned char*>(m_image);
        return VirtualProtect(base, 2 * PageSize, PAGE_EXECUTE_READ, &previous) &&
               VirtualProtect(base + 2 * PageSize, PageSize, PAGE_READONLY, &previous);
    }

    BootResult Install()
    {
        return InstallBoot(layout, invocation, session, &Prepare, failure);
    }

    bool ClosedSlots(DWORD occupied = 0) const
    {
        for (unsigned i = 0; i < SlotCount; ++i)
        {
            if (Read(layout.slots[i]) != (layout.slots[i] == occupied ? Canary : 0) ||
                Read(layout.slots[i] + sizeof(DWORD)) != Canary)
                return false;
        }
        return true;
    }

    Relocation& Row(unsigned i)
    {
        return m_relocations[i];
    }
    Signature& CheckBytes()
    {
        return m_signature;
    }

    HostLayout layout;
    BootInvocation invocation;
    BootSession session;
    BootFailure failure;

private:
    Fixture(const Fixture&);
    Fixture& operator=(const Fixture&);
    void* m_image;
    unsigned char m_backing[64];
    Relocation m_relocations[2];
    Signature m_signature;
};

bool CheckInstall()
{
    Fixture fixture;
    if (!fixture.Seal() || fixture.Install() != Installed || s_prepareCount != 1)
        return false;
    const DWORD hooks[] = {reinterpret_cast<DWORD>(&AreaStub),
                           reinterpret_cast<DWORD>(&DiscardCharacterList),
                           reinterpret_cast<DWORD>(&RejectSetCharacter),
                           reinterpret_cast<DWORD>(&DiscardReceive),
                           reinterpret_cast<DWORD>(&NoDuel),
                           reinterpret_cast<DWORD>(&DiscardSend),
                           reinterpret_cast<DWORD>(&LeaveBytesUnchanged),
                           0};
    for (unsigned i = 0; i < SlotCount; ++i)
    {
        if (Read(fixture.layout.slots[i]) != hooks[i] || Read(fixture.layout.slots[i] + 4) != Canary)
            return false;
    }
    return Read(fixture.Row(0).operandAddress) == reinterpret_cast<DWORD>(fixture.session.backing + 4) &&
           Read(fixture.Row(1).operandAddress) == reinterpret_cast<DWORD>(fixture.session.backing + 12) &&
           Protection(fixture.Row(0).operandAddress) == PAGE_EXECUTE_READ &&
           Protection(fixture.Row(1).operandAddress) == PAGE_EXECUTE_READ &&
           Protection(fixture.Row(1).operandAddress + 2) == PAGE_READONLY && fixture.Install() == AlreadyInstalled &&
           s_prepareCount == 1;
}

enum PreflightCase
{
    ForeignBase,
    ForeignSize,
    ForeignCaller,
    ForeignCallBytes,
    ForeignCell,
    BadContinuation,
    LateOperand,
    OutsideImage,
    OutsideBacking,
    NoAccess,
    GuardedPage,
    UncommittedPage,
    SlotOccupied,
    MapOccupied,
    PinFails,
    PreflightCaseCount
};

bool CheckPreflight(PreflightCase testCase)
{
    Fixture fixture;
    if (!fixture.Seal())
        return false;
    DWORD occupied = 0;
    DWORD throwaway;
    void* const secondPage = reinterpret_cast<void*>(fixture.layout.imageBase + 2 * PageSize);
    const bool inaccessible = testCase == NoAccess || testCase == GuardedPage || testCase == UncommittedPage;
    const DWORD firstSite = fixture.Row(0).operandAddress;
    const DWORD secondSite = fixture.Row(1).operandAddress;
    switch (testCase)
    {
    case ForeignBase:
        ++fixture.invocation.imageBase;
        break;
    case ForeignSize:
        --fixture.invocation.imageSize;
        break;
    case ForeignCaller:
        ++fixture.invocation.returnAddress;
        break;
    case ForeignCallBytes:
        ++fixture.layout.initCall;
        ++fixture.invocation.returnAddress;
        break;
    case ForeignCell:
        ++fixture.invocation.initAddress;
        break;
    case BadContinuation:
        ++fixture.CheckBytes().address;
        break;
    case LateOperand:
        ++fixture.Row(1).expectedValue;
        break;
    case OutsideImage:
        fixture.Row(1).operandAddress = 0xfffffffeUL;
        break;
    case OutsideBacking:
        fixture.Row(1).backingOffset = fixture.session.backingSize;
        break;
    case NoAccess:
        if (!VirtualProtect(secondPage, PageSize, PAGE_NOACCESS, &throwaway))
            return false;
        break;
    case GuardedPage:
        if (!VirtualProtect(secondPage, PageSize, PAGE_READONLY | PAGE_GUARD, &throwaway))
            return false;
        break;
    case UncommittedPage:
        if (!VirtualFree(secondPage, PageSize, MEM_DECOMMIT))
            return false;
        break;
    case SlotOccupied:
        occupied = fixture.layout.slots[SendSlot];
        Store(occupied, Canary);
        break;
    case MapOccupied:
        occupied = fixture.layout.slots[NativeMapSlot];
        Store(occupied, Canary);
        break;
    case PinFails:
        s_prepareAllowed = false;
        break;
    default:
        return false;
    }
    const BootResult expected = testCase <= ForeignCell ? ForeignHost : Rejected;
    return fixture.Install() == expected && fixture.session.state != Active && Read(firstSite) == FirstWord &&
           (inaccessible || Read(secondSite) == SecondWord) && fixture.ClosedSlots(occupied) &&
           s_prepareCount == (testCase == PinFails ? 1U : 0U);
}

bool CheckLateFault(Fault fault)
{
    Fixture fixture;
    if (!fixture.Seal())
        return false;
    s_fault = fault;
    const BootResult result = fixture.Install();
    const bool restoredFirstHalf =
        fault != SecondProtectFails || Protection(fixture.Row(1).operandAddress) == PAGE_EXECUTE_READ;
    return result == Rejected && fixture.failure.stage && restoredFirstHalf && fixture.session.state == Failed &&
           fixture.ClosedSlots(fault == SlotConflict ? s_conflictSlot : 0) && fixture.Install() == Rejected &&
           s_prepareCount == 1;
}
} // namespace

#ifdef MYDLL_RED_HOST
BootResult MyDll::Boot::InstallBoot(const HostLayout&, const BootInvocation&, BootSession&, PrepareResources,
                                    BootFailure&)
{
    return Installed;
}
void MyDll::Boot::SetMemoryHooksForTest(ProtectMemory, FlushCode) {}
#endif

int main(int argc, char* argv[])
{
    SetMemoryHooksForTest(&Protect, &Flush);
#ifndef MYDLL_RED_HOST
    if (argc == 2 && strcmp(argv[1], "--reject-child") == 0)
    {
        Fixture fixture;
        if (!fixture.Seal())
            return 3;
        ++fixture.Row(1).expectedValue;
        const BootResult result = fixture.Install();
        CompleteBoot(result, fixture.failure, fixture.layout.relocationCount);
        return 3;
    }
#else
    (void)argc;
    (void)argv;
#endif
    if (!CheckInstall())
    {
        puts("FAIL host boot installation");
        return 1;
    }
    for (unsigned i = 0; i < PreflightCaseCount; ++i)
    {
        if (!CheckPreflight(static_cast<PreflightCase>(i)))
        {
            printf("FAIL preflight case %u\n", i);
            return 1;
        }
    }
    for (unsigned fault = SecondProtectFails; fault <= SlotConflict; ++fault)
    {
        if (!CheckLateFault(static_cast<Fault>(fault)))
        {
            printf("FAIL late fault %u\n", fault);
            return 1;
        }
    }
    puts("PASS host boot");
    return 0;
}
