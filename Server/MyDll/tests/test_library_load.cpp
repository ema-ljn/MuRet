#include "BootHooks.h"

#include <stdio.h>
#include <string.h>

namespace
{
const WORD InitOrdinal = 1;

class Library
{
public:
    explicit Library(const char* path) : m_handle(LoadLibraryA(path)) {}
    ~Library()
    {
        if (m_handle)
            FreeLibrary(m_handle);
    }
    HMODULE Get() const
    {
        return m_handle;
    }

private:
    Library(const Library&);
    Library& operator=(const Library&);
    HMODULE m_handle;
};

bool CheckForeignLoader(const char* path)
{
    const Library dll(path);
    if (!dll.Get())
    {
        printf("FAIL DLL load: %lu\n", GetLastError());
        return false;
    }
    FARPROC named = GetProcAddress(dll.Get(), "Init");
    FARPROC ordinal = GetProcAddress(dll.Get(), MAKEINTRESOURCEA(InitOrdinal));
    if (!named || named != ordinal)
    {
        puts("FAIL Init name/ordinal");
        return false;
    }

    typedef BOOL(__cdecl * InitProc)();
    InitProc init;
    memcpy(&init, &named, sizeof(init));
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        return false;
    char marker[64];
    sprintf(marker, "mydll-boot-%lu-%08lx%08lx.log", GetCurrentProcessId(), created.dwHighDateTime,
            created.dwLowDateTime);
    if (GetFileAttributesA(marker) != MAXDWORD || init() != FALSE || GetFileAttributesA(marker) != MAXDWORD)
    {
        puts("FAIL foreign host disposition");
        return false;
    }
    return true;
}
} // namespace

int main(int argc, char* argv[])
{
    if (argc != 2 || !CheckForeignLoader(argv[1]))
        return 1;
    puts("PASS DLL loader");
    return 0;
}
