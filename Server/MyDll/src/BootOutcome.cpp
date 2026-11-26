#include "BootOutcome.h"

#include <stdio.h>

namespace MyDll
{
namespace Boot
{
namespace
{
const UINT BootRejectedExit = 2;

void WriteBootLog(const char* text, int length)
{
    if (length <= 0)
        return;
    char path[48];
    sprintf(path, "mydll-boot-%lu.log", GetCurrentProcessId());
    HANDLE file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written;
    WriteFile(file, text, static_cast<DWORD>(length), &written, NULL);
    CloseHandle(file);
}

void LogRejection(const BootFailure& failure)
{
    char text[192];
    const char* const stage = failure.stage ? failure.stage : "unknown";
    const DWORD pid = GetCurrentProcessId();
    const int length =
        failure.hasValue
            ? sprintf(text, "MYDLL_BOOT_REJECT PID=%lu stage=%s row=%u address=%08lx expected=%08lx observed=%08lx\r\n",
                      pid, stage, failure.row, failure.address, failure.expected, failure.observed)
            : sprintf(text, "MYDLL_BOOT_REJECT PID=%lu stage=%s row=%u address=%08lx\r\n", pid, stage, failure.row,
                      failure.address);
    WriteBootLog(text, length);
}
} // namespace

BOOL CompleteBoot(BootResult result, const BootFailure& failure, unsigned relocationCount)
{
    if (result == ForeignHost)
        return FALSE;
    if (result == Rejected)
    {
        LogRejection(failure);
        ExitProcess(BootRejectedExit);
        return FALSE;
    }
    if (result == Installed)
    {
        char text[112];
        const int length = sprintf(text, "MYDLL_BOOT_ACTIVE PID=%lu RELOC=%u SLOTS=%u\r\n", GetCurrentProcessId(),
                                   relocationCount, static_cast<unsigned>(NativeMapSlot));
        WriteBootLog(text, length);
    }
    return TRUE;
}
} // namespace Boot
} // namespace MyDll
