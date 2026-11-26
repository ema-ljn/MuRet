#include "BootOutcome.h"

#include <stdio.h>
#include <string.h>

namespace
{
bool CurrentPath(char (&path)[64])
{
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        return false;
    sprintf(path, "mydll-boot-%lu-%08lx%08lx.log", GetCurrentProcessId(), created.dwHighDateTime,
            created.dwLowDateTime);
    return true;
}

bool FreshMarker(const char* path)
{
    char oldPath[48];
    sprintf(oldPath, "mydll-boot-%lu.log", GetCurrentProcessId());
    const char oldMarker[] = "previous process\r\n";
    HANDLE oldFile =
        CreateFileA(oldPath, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (oldFile == INVALID_HANDLE_VALUE)
        return false;
    DWORD written;
    const BOOL placed = WriteFile(oldFile, oldMarker, sizeof(oldMarker) - 1, &written, NULL);
    const MyDll::Boot::BootFailure failure = {0};
    const BOOL installed = placed && written == sizeof(oldMarker) - 1
                               ? MyDll::Boot::CompleteBoot(MyDll::Boot::Installed, failure, 2814)
                               : FALSE;
    SetFilePointer(oldFile, 0, NULL, FILE_BEGIN);
    char oldBytes[sizeof(oldMarker)];
    DWORD read;
    const bool untouched = ReadFile(oldFile, oldBytes, sizeof(oldBytes), &read, NULL) &&
                           read == sizeof(oldMarker) - 1 && memcmp(oldBytes, oldMarker, read) == 0;
    CloseHandle(oldFile);
    DeleteFileA(oldPath);
    if (!installed || !untouched)
        return false;

    HANDLE fresh = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fresh == INVALID_HANDLE_VALUE)
        return false;
    char record[224];
    const BOOL readMarker = ReadFile(fresh, record, sizeof(record) - 1, &read, NULL);
    CloseHandle(fresh);
    DeleteFileA(path);
    if (!readMarker || read < 2)
        return false;
    record[read] = '\0';
    const char prefix[] = "MYDLL_BOOT_ACTIVE ";
    return strncmp(record, prefix, sizeof(prefix) - 1) == 0 && strstr(record, " START=") != NULL &&
           strstr(record, " RELOC=2814 SLOTS=7\r\n") != NULL;
}
} // namespace

int main(int argc, char* argv[])
{
    char path[64];
    if (!CurrentPath(path))
        return 3;
    if (argc == 2 && strcmp(argv[1], "--blocked-child") == 0)
    {
        if (!CreateDirectoryA(path, NULL))
            return 3;
        const MyDll::Boot::BootFailure failure = {0};
        MyDll::Boot::CompleteBoot(MyDll::Boot::Installed, failure, 2814);
        return 3;
    }
    if (!FreshMarker(path))
    {
        puts("FAIL fresh boot marker");
        return 1;
    }
    puts("PASS boot marker identity");
    return 0;
}
