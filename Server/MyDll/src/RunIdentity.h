#pragma once

#include <windows.h>

#include <stdio.h>

namespace MyDll
{
namespace Boot
{
struct RunIdentity
{
    DWORD pid;
    FILETIME start;
};

inline bool ReadRunIdentity(RunIdentity& run)
{
    FILETIME exited, kernel, user;
    run.pid = GetCurrentProcessId();
    return GetProcessTimes(GetCurrentProcess(), &run.start, &exited, &kernel, &user) != FALSE;
}

inline void FormatRunPath(char (&path)[64], const char* name, const char* extension, const RunIdentity& run)
{
    sprintf(path, "mydll-%s-%lu-%08lx%08lx.%s", name, run.pid, run.start.dwHighDateTime, run.start.dwLowDateTime,
            extension);
}
} // namespace Boot
} // namespace MyDll
