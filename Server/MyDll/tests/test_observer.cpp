#include "Observer.h"

#include <stdio.h>
#include <string.h>

namespace
{
using namespace MyDll::Boot;

class ScratchFile
{
public:
    ScratchFile() : m_ready(false)
    {
        char folder[MAX_PATH];
        const DWORD length = GetTempPathA(sizeof(folder), folder);
        if (length && length < sizeof(folder))
            m_ready = GetTempFileNameA(folder, "mxo", 0, m_path) != 0;
    }
    ~ScratchFile()
    {
        if (m_ready)
            DeleteFileA(m_path);
    }
    const char* Path() const
    {
        return m_ready ? m_path : NULL;
    }
    bool ReleaseForCreate() const
    {
        return m_ready && DeleteFileA(m_path);
    }

private:
    ScratchFile(const ScratchFile&);
    ScratchFile& operator=(const ScratchFile&);
    bool m_ready;
    char m_path[MAX_PATH];
};

bool ReadOutput(const char* path, char (&contents)[1024])
{
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD read = 0;
    const BOOL ok = ReadFile(file, contents, sizeof(contents) - 1, &read, NULL);
    CloseHandle(file);
    contents[read] = '\0';
    return ok && read < sizeof(contents) - 1;
}

unsigned Lines(const char* contents)
{
    unsigned count = 0;
    for (const char* byte = contents; *byte; ++byte)
    {
        if (*byte == '\n')
            ++count;
    }
    return count;
}

bool CheckConfig()
{
    const ScratchFile ini;
    if (!ini.Path())
        return false;
    const ObserverOptions defaults = ReadObserverOptions(ini.Path());
    if (defaults.enabled || !defaults.receive || defaults.send || !defaults.includeSubcode || !defaults.logMalformed ||
        defaults.maxRows != 4096)
        return false;

    const char section[] = "Observer";
    if (!WritePrivateProfileStringA(section, "Enabled", "1", ini.Path()) ||
        !WritePrivateProfileStringA(section, "Receive", "0", ini.Path()) ||
        !WritePrivateProfileStringA(section, "Send", "1", ini.Path()) ||
        !WritePrivateProfileStringA(section, "IncludeSubcode", "0", ini.Path()) ||
        !WritePrivateProfileStringA(section, "LogMalformed", "0", ini.Path()) ||
        !WritePrivateProfileStringA(section, "MaxRows", "999999", ini.Path()))
        return false;
    const ObserverOptions custom = ReadObserverOptions(ini.Path());
    return custom.enabled && !custom.receive && custom.send && !custom.includeSubcode && !custom.logMalformed &&
           custom.maxRows == 65536;
}

bool CheckReceive()
{
    const ScratchFile output;
    if (!output.ReleaseForCreate())
        return false;
    ObserverOptions options = {false, true, false, true, true, 4};
    if (OpenObserver(output.Path(), options) || GetFileAttributesA(output.Path()) != MAXDWORD)
        return false;
    options.enabled = true;
    if (!OpenObserver(output.Path(), options))
        return false;
    const unsigned char login[] = {0xc1, 10, 0xf1, 1, 'S', 'E', 'C', 'R', 'E', 'T'};
    unsigned char bytes[sizeof(login)];
    memcpy(bytes, login, sizeof(bytes));
    SetLastError(1234);
    const DWORD rx = ObserveReceive(0xf1, reinterpret_cast<DWORD>(bytes), sizeof(bytes), 4321, 0, 0);
    const DWORD last = GetLastError();
    const DWORD tx = ObserveSend(4321, reinterpret_cast<char*>(bytes), sizeof(bytes));
    unsigned char wide[] = {0xc2, 0, 4, 0x55};
    ObserveReceive(0x55, reinterpret_cast<DWORD>(wide), sizeof(wide), 0, 0, 0);
    void* inaccessible = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    if (!inaccessible)
    {
        CloseObserver();
        return false;
    }
    ObserveReceive(0, reinterpret_cast<DWORD>(inaccessible), 16, 0, 0, 0);
    VirtualFree(inaccessible, 0, MEM_RELEASE);
    const unsigned char malformed[] = {'X', 'S', 'E', 'C', 'R', 'E', 'T'};
    ObserveReceive(0, reinterpret_cast<DWORD>(malformed), sizeof(malformed), 0, 0, 0);
    ObserveReceive(0xf1, reinterpret_cast<DWORD>(bytes), sizeof(bytes), 0, 0, 0);
    CloseObserver();

    char report[1024];
    return rx == 0x80004005UL && tx == 1 && last == 1234 && memcmp(login, bytes, sizeof(bytes)) == 0 &&
           ReadOutput(output.Path(), report) && Lines(report) == 5 && strstr(report, "SECRET") == NULL &&
           strstr(report, "4321") == NULL && strstr(report, "\tRX\tBLOCKED_BY_STUB\tC1\tF1\t01\t10\tok\t") != NULL &&
           strstr(report, "\tRX\tBLOCKED_BY_STUB\tC2\t55\t-\t4\tok\t") != NULL &&
           strstr(report, "\tRX\tBLOCKED_BY_STUB\t-\t-\t-\t16\tunreadable\t") != NULL &&
           strstr(report, "\tRX\tBLOCKED_BY_STUB\t-\t-\t-\t7\tmalformed\t") != NULL;
}

bool CheckSendAndMalformedOptions()
{
    const ScratchFile output;
    const ObserverOptions options = {true, false, true, false, false, 2};
    if (!output.ReleaseForCreate() || !OpenObserver(output.Path(), options))
        return false;
    unsigned char packet[] = {0xc1, 4, 0xf3, 1};
    ObserveReceive(0xf3, reinterpret_cast<DWORD>(packet), sizeof(packet), 0, 0, 0);
    ObserveSend(0, reinterpret_cast<char*>(packet), 1);
    ObserveSend(0, reinterpret_cast<char*>(packet), sizeof(packet));
    packet[0] = 0xc3;
    ObserveSend(0, reinterpret_cast<char*>(packet), sizeof(packet));
    CloseObserver();
    char report[1024];
    return ReadOutput(output.Path(), report) && Lines(report) == 3 &&
           strstr(report, "\tTX\tBLOCKED_BY_STUB\tC1\tF3\t-\t4\tok\t") != NULL &&
           strstr(report, "\tTX\tBLOCKED_BY_STUB\tC3\t-\t-\t4\topaque\t") != NULL;
}
} // namespace

#ifdef MYDLL_RED_OBSERVER
ObserverOptions MyDll::Boot::ReadObserverOptions(const char*)
{
    const ObserverOptions absent = {false, false, false, false, false, 0};
    return absent;
}
bool MyDll::Boot::OpenObserver(const char*, const ObserverOptions&)
{
    return false;
}
void MyDll::Boot::CloseObserver() {}
DWORD __cdecl MyDll::Boot::ObserveReceive(DWORD, DWORD, DWORD, int, DWORD, DWORD)
{
    return 0x80004005UL;
}
DWORD __cdecl MyDll::Boot::ObserveSend(int, char*, DWORD)
{
    return 1;
}
#endif

int main(int argc, char* argv[])
{
#ifndef MYDLL_RED_OBSERVER
    if (argc == 2 && strcmp(argv[1], "--config-disabled-child") == 0)
    {
        BootSession session;
        memset(&session, 0, sizeof(session));
        ConfigureObserver(session);
        if (session.receive || session.send)
            return 1;
        puts("PASS missing or disabled INI");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--config-child") == 0)
    {
        BootSession session;
        memset(&session, 0, sizeof(session));
        ConfigureObserver(session);
        if (session.receive != &ObserveReceive || session.send)
        {
            puts("FAIL INI beside executable");
            return 1;
        }
        const unsigned char frame[] = {0xc1, 4, 0xf3, 1};
        session.receive(0xf3, reinterpret_cast<DWORD>(frame), sizeof(frame), 0, 0, 0);
        CloseObserver();
        puts("PASS INI beside executable");
        return 0;
    }
#else
    (void)argc;
    (void)argv;
#endif
    if (!CheckConfig())
    {
        puts("FAIL observer config");
        return 1;
    }
    if (!CheckReceive() || !CheckSendAndMalformedOptions())
    {
        puts("FAIL observer rows");
        return 1;
    }
    puts("PASS observer: opt-in, metadata only, bounded rows, unchanged stub behavior");
    return 0;
}
