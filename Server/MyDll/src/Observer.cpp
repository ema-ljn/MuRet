#include "Observer.h"
#include "ExecutableDirectory.h"
#include "RunIdentity.h"

#include <excpt.h>
#include <string.h>

namespace MyDll
{
namespace Boot
{
namespace
{
const char IniName[] = "debug_mydll.ini";
const char IniSection[] = "Observer";
const unsigned DefaultRows = 4096;
const unsigned MaximumRows = 65536;
const char Header[] = "row\tdir\tdecision\tframe\topcode\tsubcode\tlength\tstatus\tbusy_drops\r\n";

const int Missing = -1;
const int ShortPlainFrame = 0xc1;
const int WidePlainFrame = 0xc2;
const int ShortOpaqueFrame = 0xc3;
const int WideOpaqueFrame = 0xc4;
const unsigned AccountOpcode = 0xf1;
const unsigned CharacterOpcode = 0xf3;

HANDLE s_file = INVALID_HANDLE_VALUE;
ObserverOptions s_options;
LONG s_writer;
LONG s_busyDrops;
unsigned s_rows;

enum FrameState
{
    Parsed,
    Opaque,
    Short,
    Malformed,
    Unreadable
};

struct Metadata
{
    int frame;
    int opcode;
    int subcode;
    FrameState state;
};

bool OptionEnabled(const char* ini, const char* name, int defaultValue)
{
    return GetPrivateProfileIntA(IniSection, name, defaultValue, ini) == 1;
}

bool WriteAll(HANDLE file, const char* bytes, DWORD length)
{
    DWORD written = 0;
    return WriteFile(file, bytes, length, &written, NULL) && written == length;
}

bool ReadablePage(DWORD protection)
{
    if (protection & PAGE_GUARD)
        return false;
    switch (protection & 0xff)
    {
    case PAGE_READONLY:
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    default:
        return false;
    }
}

bool ReadableSpan(DWORD address, DWORD bytes)
{
    if (!address || !bytes || bytes - 1 > MAXDWORD - address)
        return false;
    while (bytes)
    {
        MEMORY_BASIC_INFORMATION region;
        if (!VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) ||
            region.State != MEM_COMMIT || !ReadablePage(region.Protect))
            return false;
        const DWORD offset = address - reinterpret_cast<DWORD>(region.BaseAddress);
        const DWORD available = region.RegionSize - offset;
        if (bytes <= available)
            return true;
        address += available;
        bytes -= available;
    }
    return true;
}

int HeaderFaultFilter(DWORD exception)
{
    return exception == EXCEPTION_ACCESS_VIOLATION || exception == EXCEPTION_IN_PAGE_ERROR ||
                   exception == EXCEPTION_GUARD_PAGE
               ? EXCEPTION_EXECUTE_HANDLER
               : EXCEPTION_CONTINUE_SEARCH;
}

bool CopyHeader(DWORD address, unsigned char* out, DWORD size)
{
    if (!ReadableSpan(address, size))
        return false;
    __try
    {
        const volatile unsigned char* bytes = reinterpret_cast<const volatile unsigned char*>(address);
        for (DWORD i = 0; i < size; ++i)
            out[i] = bytes[i];
    }
    __except (HeaderFaultFilter(GetExceptionCode()))
    {
        return false;
    }
    return true;
}

Metadata ReadMetadata(DWORD packet, DWORD length, bool includeSubcode)
{
    Metadata metadata = {Missing, Missing, Missing, Short};
    if (!length)
        return metadata;
    unsigned char header[4];
    if (!CopyHeader(packet, header, 1))
    {
        metadata.state = Unreadable;
        return metadata;
    }
    metadata.frame = header[0];
    if (metadata.frame == ShortOpaqueFrame || metadata.frame == WideOpaqueFrame)
    {
        metadata.state = Opaque;
        return metadata;
    }
    if (metadata.frame != ShortPlainFrame && metadata.frame != WidePlainFrame)
    {
        metadata.frame = Missing;
        metadata.state = Malformed;
        return metadata;
    }
    const bool wide = metadata.frame == WidePlainFrame;
    const DWORD headerSize = wide ? 4 : 3;
    if (length < headerSize)
        return metadata;
    if (!CopyHeader(packet, header, headerSize))
    {
        metadata.state = Unreadable;
        return metadata;
    }
    const DWORD declared = wide ? 256UL * header[1] + header[2] : header[1];
    if (header[0] != metadata.frame || declared < headerSize)
    {
        metadata.state = Malformed;
        return metadata;
    }
    const unsigned opcode = header[headerSize - 1];
    metadata.opcode = static_cast<int>(opcode);
    metadata.state = Parsed;
    if (includeSubcode && !wide && length > headerSize && declared > headerSize &&
        (opcode == AccountOpcode || opcode == CharacterOpcode) && packet <= MAXDWORD - headerSize)
    {
        unsigned char subcode;
        if (CopyHeader(packet + headerSize, &subcode, 1))
            metadata.subcode = subcode;
    }
    return metadata;
}

const char* StateName(FrameState state)
{
    switch (state)
    {
    case Parsed:
        return "ok";
    case Opaque:
        return "opaque";
    case Short:
        return "short";
    case Unreadable:
        return "unreadable";
    default:
        return "malformed";
    }
}

void HexField(int value, char (&text)[4])
{
    if (value == Missing)
        strcpy(text, "-");
    else
        sprintf(text, "%02X", static_cast<unsigned>(value));
}

const char* DecisionName(ObservationDecision decision)
{
    switch (decision)
    {
    case Delegated:
        return "DELEGATED_TO_GS";
    case BlockedByProfile:
        return "BLOCKED_BY_PROFILE";
    default:
        return "BLOCKED_BY_STUB";
    }
}

void WriteRow(const char* direction, DWORD length, const Metadata& metadata, ObservationDecision decision)
{
    char frame[4], opcode[4], subcode[4];
    HexField(metadata.frame, frame);
    HexField(metadata.opcode, opcode);
    HexField(metadata.subcode, subcode);
    char row[144];
    const LONG lost = InterlockedExchange(&s_busyDrops, 0);
    const int size = sprintf(row, "%u\t%s\t%s\t%s\t%s\t%s\t%lu\t%s\t%ld\r\n", s_rows + 1, direction,
                             DecisionName(decision), frame, opcode, subcode, length, StateName(metadata.state), lost);
    if (size <= 0 || !WriteAll(s_file, row, static_cast<DWORD>(size)))
    {
        CloseObserver();
        OutputDebugStringA("MyDll observer stopped after a log write failure.");
        return;
    }
    ++s_rows;
}

void Record(bool receive, DWORD packet, DWORD length, ObservationDecision decision)
{
    if (!(receive ? s_options.receive : s_options.send))
        return;
    if (InterlockedExchange(&s_writer, 1) != 0)
    {
        InterlockedIncrement(&s_busyDrops);
        return;
    }
    if (s_file != INVALID_HANDLE_VALUE && s_rows < s_options.maxRows)
    {
        const Metadata metadata = ReadMetadata(packet, length, s_options.includeSubcode);
        if (metadata.state == Parsed || metadata.state == Opaque || s_options.logMalformed)
            WriteRow(receive ? "RX" : "TX", length, metadata, decision);
    }
    InterlockedExchange(&s_writer, 0);
}

} // namespace

ObserverOptions ReadObserverOptions(const char* ini)
{
    ObserverOptions options = {false, true, false, true, true, DefaultRows};
    if (!ini)
        return options;
    options.enabled = OptionEnabled(ini, "Enabled", 0);
    options.receive = OptionEnabled(ini, "Receive", 1);
    options.send = OptionEnabled(ini, "Send", 0);
    options.includeSubcode = OptionEnabled(ini, "IncludeSubcode", 1);
    options.logMalformed = OptionEnabled(ini, "LogMalformed", 1);
    const unsigned requested = GetPrivateProfileIntA(IniSection, "MaxRows", DefaultRows, ini);
    if (requested)
        options.maxRows = requested > MaximumRows ? MaximumRows : requested;
    return options;
}

bool OpenObserver(const char* path, const ObserverOptions& options)
{
    if (!path || !options.enabled || (!options.receive && !options.send) || !options.maxRows ||
        options.maxRows > MaximumRows || s_file != INVALID_HANDLE_VALUE)
        return false;
    HANDLE file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    if (!WriteAll(file, Header, sizeof(Header) - 1))
    {
        CloseHandle(file);
        DeleteFileA(path);
        return false;
    }
    s_options = options;
    s_rows = 0;
    s_writer = 0;
    s_busyDrops = 0;
    s_file = file;
    return true;
}

void CloseObserver()
{
    if (s_file == INVALID_HANDLE_VALUE)
        return;
    CloseHandle(s_file);
    s_file = INVALID_HANDLE_VALUE;
}

void ConfigureObserver(BootSession& session, bool installClosedHooks)
{
    char path[MAX_PATH];
    DWORD directoryLength;
    if (!ExecutableDirectory(path, directoryLength) || sizeof(IniName) > sizeof(path) - directoryLength)
        return;
    memcpy(path + directoryLength, IniName, sizeof(IniName));
    const DWORD attributes = GetFileAttributesA(path);
    if (attributes == MAXDWORD || (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return;
    const ObserverOptions options = ReadObserverOptions(path);
    if (!options.enabled || (!options.receive && !options.send))
        return;
    RunIdentity run;
    char basename[64];
    if (!ReadRunIdentity(run))
        return;
    FormatRunPath(basename, "observe", "tsv", run);
    if (strlen(basename) < sizeof(path) - directoryLength)
    {
        strcpy(path + directoryLength, basename);
        if (OpenObserver(path, options))
        {
            if (installClosedHooks)
            {
                session.receive = options.receive ? &ObserveReceive : NULL;
                session.send = options.send ? &ObserveSend : NULL;
            }
            return;
        }
    }
    OutputDebugStringA("MyDll observer was requested but its log could not be opened.");
}

void RecordObservation(bool receive, DWORD packet, DWORD length, ObservationDecision decision)
{
    const DWORD lastError = GetLastError();
    Record(receive, packet, length, decision);
    SetLastError(lastError);
}

DWORD __cdecl ObserveReceive(DWORD proto, DWORD packet, DWORD length, int index, DWORD encrypted, DWORD serial)
{
    RecordObservation(true, packet, length, BlockedByStub);
    return DiscardReceive(proto, packet, length, index, encrypted, serial);
}

DWORD __cdecl ObserveSend(int index, char* packet, DWORD length)
{
    RecordObservation(false, reinterpret_cast<DWORD>(packet), length, BlockedByStub);
    return DiscardSend(index, packet, length);
}
} // namespace Boot
} // namespace MyDll
