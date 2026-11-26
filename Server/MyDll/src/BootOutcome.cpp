#include "BootOutcome.h"
#include "RunIdentity.h"

#include <stdio.h>

namespace MyDll
{
namespace Boot
{
namespace
{
const UINT BootRejectedExit = 2;

bool WriteRunLog(const RunIdentity& run, const char* kind, const char* text, int length)
{
    if (length <= 0)
        return false;
    char path[64];
    char temporary[64];
    FormatRunPath(path, kind, "log", run);
    FormatRunPath(temporary, kind, "tmp", run);
    HANDLE file = CreateFileA(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    const DWORD size = static_cast<DWORD>(length);
    DWORD written = 0;
    const bool complete = WriteFile(file, text, size, &written, NULL) && written == size;
    const BOOL closed = CloseHandle(file);
    if (complete && closed && MoveFileA(temporary, path))
        return true;
    DeleteFileA(temporary);
    return false;
}

void LogRejection(const BootFailure& failure)
{
    RunIdentity run;
    if (!ReadRunIdentity(run))
        return;
    char text[224];
    const char* const stage = failure.stage ? failure.stage : "unknown";
    const DWORD high = run.start.dwHighDateTime;
    const DWORD low = run.start.dwLowDateTime;
    const int length =
        failure.hasValue
            ? sprintf(text,
                      "MYDLL_BOOT_REJECT PID=%lu START=%08lx%08lx stage=%s row=%u address=%08lx expected=%08lx "
                      "observed=%08lx\r\n",
                      run.pid, high, low, stage, failure.row, failure.address, failure.expected, failure.observed)
            : sprintf(text, "MYDLL_BOOT_REJECT PID=%lu START=%08lx%08lx stage=%s row=%u address=%08lx\r\n", run.pid,
                      high, low, stage, failure.row, failure.address);
    WriteRunLog(run, "boot", text, length);
}
} // namespace

BOOL CompleteBoot(BootResult result, const BootFailure& failure, unsigned relocationCount, unsigned slotCount)
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
        RunIdentity run;
        if (ReadRunIdentity(run))
        {
            char text[128];
            const int length =
                sprintf(text, "MYDLL_BOOT_ACTIVE PID=%lu START=%08lx%08lx RELOC=%u SLOTS=%u\r\n", run.pid,
                        run.start.dwHighDateTime, run.start.dwLowDateTime, relocationCount, slotCount);
            if (WriteRunLog(run, "boot", text, length))
                return TRUE;
        }
        OutputDebugStringA("MyDll could not publish a complete boot marker.");
        ExitProcess(BootRejectedExit);
        return FALSE;
    }
    return TRUE;
}

void ReportTerrainLoad(bool complete, unsigned loaded, unsigned failedMap)
{
    RunIdentity run;
    if (ReadRunIdentity(run))
    {
        char text[144];
        const int length = complete ? sprintf(text, "MYDLL_MAPS_LOADED COUNT=%u\r\n", loaded)
                                    : sprintf(text, "MYDLL_MAPS_FAILED COUNT=%u MAP=%u\r\n", loaded, failedMap);
        if (WriteRunLog(run, "maps", text, length) && complete)
            return;
    }
    OutputDebugStringA("MyDll could not complete terrain loading or publish its marker.");
    ExitProcess(BootRejectedExit);
}
} // namespace Boot
} // namespace MyDll
