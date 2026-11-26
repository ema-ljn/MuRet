#pragma once

#include <string.h>
#include <windows.h>

namespace MyDll
{
namespace Boot
{
inline bool ExecutableDirectory(char (&path)[MAX_PATH], DWORD& directoryLength)
{
    const DWORD length = GetModuleFileNameA(NULL, path, sizeof(path));
    if (!length || length >= sizeof(path))
        return false;
    const char* const separator = strrchr(path, '\\');
    if (!separator)
        return false;
    directoryLength = static_cast<DWORD>(separator - path + 1);
    return true;
}
} // namespace Boot
} // namespace MyDll
