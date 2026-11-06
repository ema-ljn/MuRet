#pragma once

#include <cstddef>
#include <cstdint>

// Sustituto de API solo para tests: no representa un proceso ni acredita el ABI de Win32.
using DWORD = std::uint32_t;
using SIZE_T = std::size_t;
using ULONG_PTR = std::uintptr_t;
using BOOL = int;
using HANDLE = void*;
using HWND = void*;
using LPCVOID = const void*;
inline constexpr DWORD MAXDWORD = 0xffffffffU;
inline constexpr DWORD ERROR_PARTIAL_COPY = 299;
inline constexpr DWORD ERROR_BUSY = 170;
inline constexpr DWORD STILL_ACTIVE = 259;
inline constexpr DWORD PROCESS_QUERY_INFORMATION = 0x400;
inline constexpr DWORD PROCESS_VM_READ = 0x10;
inline constexpr DWORD THREAD_QUERY_INFORMATION = 0x40;
inline constexpr DWORD THREAD_GET_CONTEXT = 8;
inline constexpr DWORD THREAD_SUSPEND_RESUME = 2;
inline constexpr DWORD CONTEXT_CONTROL = 1;
inline constexpr DWORD CONTEXT_FLOATING_POINT = 8;
inline constexpr BOOL FALSE = 0;
struct CONTEXT
{
    DWORD ContextFlags;
    DWORD Eip;
    struct
    {
        DWORD ControlWord;
    } FloatSave;
};
HWND FindWindowA(const char*, const char*);
DWORD GetWindowThreadProcessId(HWND, DWORD*);
HANDLE OpenProcess(DWORD, BOOL, DWORD);
HANDLE OpenThread(DWORD, BOOL, DWORD);
BOOL ReadProcessMemory(HANDLE, LPCVOID, void*, SIZE_T, SIZE_T*);
BOOL CloseHandle(HANDLE);
DWORD GetLastError();
DWORD GetTickCount();
void Sleep(DWORD);
DWORD SuspendThread(HANDLE);
BOOL GetThreadContext(HANDLE, CONTEXT*);
DWORD ResumeThread(HANDLE);
BOOL GetExitCodeThread(HANDLE, DWORD*);
