#include "HostBoot.h"

#include <string.h>

namespace MyDll
{
namespace Boot
{
namespace
{
const DWORD WordSize = sizeof(DWORD);
const DWORD InitCallSize = 6;

#ifdef MYDLL_HOST_TEST
ProtectMemory s_protect = &VirtualProtect;
FlushCode s_flush = &FlushInstructionCache;
#else
BOOL(WINAPI* const s_protect)(LPVOID, DWORD, DWORD, PDWORD) = &VirtualProtect;
BOOL(WINAPI* const s_flush)(HANDLE, LPCVOID, DWORD) = &FlushInstructionCache;
#endif

bool AllowsAccess(DWORD protection, bool writable)
{
    if (protection & PAGE_GUARD)
        return false;
    switch (protection & 0xff)
    {
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    case PAGE_READONLY:
    case PAGE_EXECUTE_READ:
        return !writable;
    default:
        return false;
    }
}

bool CanAccess(const HostLayout& layout, DWORD address, DWORD size, bool writable)
{
    if (address < layout.imageBase || size == 0 || size - 1 > MAXDWORD - address)
        return false;
    const DWORD relative = address - layout.imageBase;
    if (relative > layout.imageSize || size > layout.imageSize - relative)
        return false;

    while (size)
    {
        MEMORY_BASIC_INFORMATION region;
        if (!VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) ||
            reinterpret_cast<DWORD>(region.AllocationBase) != layout.imageBase || region.State != MEM_COMMIT ||
            !AllowsAccess(region.Protect, writable))
            return false;
        const DWORD offset = address - reinterpret_cast<DWORD>(region.BaseAddress);
        const DWORD available = region.RegionSize - offset;
        if (size <= available)
            return true;
        address += available;
        size -= available;
    }
    return true;
}

DWORD ReadWord(DWORD address)
{
    DWORD value;
    memcpy(&value, reinterpret_cast<const void*>(address), WordSize);
    return value;
}

bool IsBootCaller(const HostLayout& layout, const BootInvocation& invocation)
{
    if (layout.imageBase != invocation.imageBase || layout.imageSize != invocation.imageSize ||
        invocation.returnAddress != layout.initCall + InitCallSize || !invocation.initAddress ||
        !CanAccess(layout, layout.initCall, InitCallSize, false) ||
        !CanAccess(layout, layout.initCell, WordSize, false))
        return false;
    const unsigned char* const call = reinterpret_cast<const unsigned char*>(layout.initCall);
    return call[0] == 0xff && call[1] == 0x15 && ReadWord(layout.initCall + 2) == layout.initCell &&
           ReadWord(layout.initCell) == invocation.initAddress;
}

bool Fault(BootFailure& failure, const char* stage, DWORD address = 0, unsigned row = 0)
{
    failure.stage = stage;
    failure.address = address;
    failure.row = row;
    return false;
}

bool CheckWord(BootFailure& failure, const char* stage, DWORD address, DWORD expected, unsigned row)
{
    const DWORD observed = ReadWord(address);
    if (observed == expected)
        return true;
    failure.expected = expected;
    failure.observed = observed;
    failure.hasValue = true;
    return Fault(failure, stage, address, row);
}

bool CheckSlots(const HostLayout& layout, BootFailure& failure)
{
    for (unsigned i = 0; i < SlotCount; ++i)
    {
        const DWORD address = layout.slots[i];
        if ((address % WordSize) != 0 || !CanAccess(layout, address, WordSize, i != NativeMapSlot))
            return Fault(failure, "slot_access", address, i);
        if (!CheckWord(failure, "slot_occupied", address, 0, i))
            return false;
    }
    return true;
}

bool Preflight(const HostLayout& layout, const BootSession& session, BootFailure& failure)
{
    if (!session.backing || !session.backingSize)
        return Fault(failure, "backing");
    for (unsigned i = 0; i < layout.signatureCount; ++i)
    {
        const Signature& signature = layout.signatures[i];
        if (!CanAccess(layout, signature.address, signature.size, false) ||
            memcmp(reinterpret_cast<const void*>(signature.address), signature.bytes, signature.size) != 0)
            return Fault(failure, "signature", signature.address, i);
    }
    for (unsigned row = 0; row < layout.relocationCount; ++row)
    {
        const Relocation& entry = layout.relocations[row];
        if (entry.backingOffset >= session.backingSize || !CanAccess(layout, entry.operandAddress, WordSize, false))
            return Fault(failure, "operand_range", entry.operandAddress, row);
        if (!CheckWord(failure, "operand_preimage", entry.operandAddress, entry.expectedValue, row))
            return false;
    }
    return CheckSlots(layout, failure);
}

bool Prepare(PrepareResources prepareResources, BootFailure& failure)
{
    if (prepareResources && prepareResources())
        return true;
    return Fault(failure, "resources");
}

bool WriteOperand(DWORD address, DWORD replacement, DWORD pageSize, unsigned row, BootFailure& failure)
{
    const DWORD firstPage = address - (address % pageSize);
    const DWORD lastByte = address + WordSize - 1;
    const DWORD lastPage = lastByte - (lastByte % pageSize);
    void* const first = reinterpret_cast<void*>(firstPage);
    void* const last = reinterpret_cast<void*>(lastPage);
    const bool crosses = firstPage != lastPage;
    DWORD firstProtection = 0;
    DWORD lastProtection = 0;
    if (!s_protect(first, pageSize, PAGE_EXECUTE_READWRITE, &firstProtection))
        return Fault(failure, "protect_first", address, row);
    if (crosses && !s_protect(last, pageSize, PAGE_EXECUTE_READWRITE, &lastProtection))
    {
        DWORD ignored;
        const BOOL restored = s_protect(first, pageSize, firstProtection, &ignored);
        return Fault(failure, restored ? "protect_second" : "restore_first", address, row);
    }

    memcpy(reinterpret_cast<void*>(address), &replacement, WordSize);
    DWORD ignored;
    const bool lastRestored = !crosses || s_protect(last, pageSize, lastProtection, &ignored) != FALSE;
    const bool firstRestored = s_protect(first, pageSize, firstProtection, &ignored) != FALSE;
    if (!firstRestored || !lastRestored)
        return Fault(failure, firstRestored ? "restore_second" : "restore_first", address, row);
    return true;
}

bool Relocate(const HostLayout& layout, const BootSession& session, BootFailure& failure)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    if (info.dwPageSize < WordSize)
        return Fault(failure, "page_size");
    for (unsigned i = 0; i < layout.relocationCount; ++i)
    {
        const Relocation& row = layout.relocations[i];
        const DWORD replacement = reinterpret_cast<DWORD>(session.backing + row.backingOffset);
        if (!CanAccess(layout, row.operandAddress, WordSize, false))
            return Fault(failure, "operand_access", row.operandAddress, i);
        if (!CheckWord(failure, "operand_changed", row.operandAddress, row.expectedValue, i) ||
            !WriteOperand(row.operandAddress, replacement, info.dwPageSize, i, failure))
            return false;
    }
    if (!s_flush(GetCurrentProcess(), NULL, 0))
        return Fault(failure, "flush");
    for (unsigned check = 0; check < layout.relocationCount; ++check)
    {
        const Relocation& row = layout.relocations[check];
        if (!CanAccess(layout, row.operandAddress, WordSize, false))
            return Fault(failure, "readback_access", row.operandAddress, check);
        if (!CheckWord(failure, "operand_readback", row.operandAddress,
                       reinterpret_cast<DWORD>(session.backing + row.backingOffset), check))
            return false;
    }
    return true;
}

bool Publish(const HostLayout& layout, BootFailure& failure)
{
    if (!CheckSlots(layout, failure))
        return false;
    const DWORD hooks[NativeMapSlot] = {reinterpret_cast<DWORD>(&AreaStub),
                                        reinterpret_cast<DWORD>(&DiscardCharacterList),
                                        reinterpret_cast<DWORD>(&RejectSetCharacter),
                                        reinterpret_cast<DWORD>(&DiscardReceive),
                                        reinterpret_cast<DWORD>(&NoDuel),
                                        reinterpret_cast<DWORD>(&DiscardSend),
                                        reinterpret_cast<DWORD>(&LeaveBytesUnchanged)};
    for (unsigned i = 0; i < NativeMapSlot; ++i)
    {
        const DWORD address = layout.slots[i];
        PVOID const old =
            InterlockedCompareExchange(reinterpret_cast<PVOID*>(address), reinterpret_cast<PVOID>(hooks[i]), NULL);
        if (old)
        {
            failure.observed = reinterpret_cast<DWORD>(old);
            failure.hasValue = true;
            return Fault(failure, "slot_changed", address, i);
        }
    }
    for (unsigned check = 0; check < SlotCount; ++check)
    {
        if (!CheckWord(failure, "slot_readback", layout.slots[check], check == NativeMapSlot ? 0 : hooks[check], check))
            return false;
    }
    return true;
}
} // namespace

BootResult InstallBoot(const HostLayout& layout, const BootInvocation& invocation, BootSession& session,
                       PrepareResources prepareResources, BootFailure& failure)
{
    memset(&failure, 0, sizeof(failure));
    if (!IsBootCaller(layout, invocation))
        return ForeignHost;
    if (session.state == Active)
        return AlreadyInstalled;
    if (session.state != Cold)
    {
        Fault(failure, "state");
        return Rejected;
    }
    session.state = Installing;
    if (!Preflight(layout, session, failure) || !Prepare(prepareResources, failure) ||
        !Relocate(layout, session, failure) || !Publish(layout, failure))
    {
        session.state = Failed;
        return Rejected;
    }
    session.state = Active;
    return Installed;
}

#ifdef MYDLL_HOST_TEST
void SetMemoryHooksForTest(ProtectMemory protect, FlushCode flush)
{
    s_protect = protect;
    s_flush = flush;
}
#endif
} // namespace Boot
} // namespace MyDll
