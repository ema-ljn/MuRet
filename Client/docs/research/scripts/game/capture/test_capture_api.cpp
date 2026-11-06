#include "test_capture_windows.h"

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" int __real_fflush(FILE*);
extern "C" int __real_fclose(FILE*);

namespace
{
std::array<const char*, 200000> s_events{};
std::size_t s_eventCount = 0;
DWORD s_lastError = 0;
DWORD s_ticks = 100;
unsigned s_resumes = 0;
bool s_firstPause = false;
bool s_owed = false;
HANDLE const Process = reinterpret_cast<HANDLE>(1);
HANDLE const Thread = reinterpret_cast<HANDLE>(2);

bool Scenario(const char* name)
{
    const char* selected = std::getenv("MURET_CAPTURE_SCENARIO");
    return selected != nullptr && std::strcmp(selected, name) == 0;
}

void Record(const char* event)
{
    if (s_eventCount < s_events.size())
        s_events[s_eventCount++] = event;
}

void CheckIo()
{
    if (s_firstPause)
        Record("io_before_first_resume");
}

struct Trace
{
    ~Trace()
    {
        const char* path = std::getenv("MURET_CAPTURE_TRACE");
        FILE* file = path == nullptr ? nullptr : std::fopen(path, "w");
        if (file == nullptr)
            return;
        for (std::size_t index = 0; index < s_eventCount; ++index)
            std::fprintf(file, "%s\n", s_events[index]);
        if (s_owed)
            std::fputs("abandoned_suspension\n", file);
        __real_fclose(file);
    }
} s_trace;
} // namespace

HWND FindWindowA(const char*, const char*)
{
    Record("window");
    return Scenario("no_window") ? nullptr : Process;
}
DWORD GetWindowThreadProcessId(HWND, DWORD* pid)
{
    *pid = 42;
    return 43;
}
HANDLE OpenProcess(DWORD, BOOL, DWORD)
{
    Record("open_process");
    s_lastError = 5;
    return Scenario("open_process") ? nullptr : Process;
}
HANDLE OpenThread(DWORD, BOOL, DWORD)
{
    Record("open_thread");
    return Thread;
}
BOOL ReadProcessMemory(HANDLE, LPCVOID pointer, void* output, SIZE_T size, SIZE_T* count)
{
    Record("read");
    const auto address = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(pointer));
    constexpr std::array<unsigned char, 16> Camera{0x55, 0x8b, 0xec, 0x83, 0xec, 0x58, 0xa0, 0x99,
                                                   0x68, 0xd3, 0x07, 0xc7, 0x05, 0x48, 0x5d, 0x6d};
    constexpr std::array<unsigned char, 16> Inverse{0x55, 0x8b, 0xec, 0x8b, 0x4d, 0x0c, 0x8b, 0x45,
                                                    0x08, 0x8b, 0x55, 0x10, 0xd9, 0x41, 0x20, 0xd8};
    std::memset(output, 0, size);
    *count = size;
    if (address == 0x00635ae0 || address == 0x005fcc30)
    {
        std::memcpy(output, address == 0x00635ae0 ? Camera.data() : Inverse.data(), size);
        if (Scenario("signature"))
            static_cast<unsigned char*>(output)[0] = 0;
        return 1;
    }
    if (address == 0x006c9dd0 && (Scenario("short_read") || Scenario("read_error")))
    {
        *count = size - 1;
        s_lastError = 123;
        return Scenario("short_read") ? 1 : 0;
    }
    if (address == 0x006d5dc8)
    {
        constexpr DWORD Scene = 5;
        std::memcpy(output, &Scene, sizeof(Scene));
    }
    if (address == 0x0743a170)
    {
        const DWORD hero = Scenario("hero") ? 0x10000U : Scenario("overflow") ? 0xfffffff0U : 0U;
        std::memcpy(output, &hero, sizeof(hero));
    }
    if (address == 0x10010 && size == 12)
    {
        constexpr std::array<float, 3> Position{100, 200, 300};
        std::memcpy(output, Position.data(), size);
    }
    return 1;
}
BOOL CloseHandle(HANDLE handle)
{
    Record(handle == Thread ? "close_thread" : "close_process");
    if (s_owed && handle == Thread)
        Record("close_while_owed");
    s_lastError = 6;
    return !Scenario(handle == Thread ? "close_thread" : "close_process");
}
DWORD GetLastError()
{
    Record("last_error");
    return s_lastError;
}
DWORD GetTickCount()
{
    Record("tick");
    return s_ticks++;
}
void Sleep(DWORD duration)
{
    Record("sleep");
    s_ticks += duration;
}
DWORD SuspendThread(HANDLE)
{
    Record("suspend");
    s_lastError = 111;
    if (Scenario("suspend"))
        return MAXDWORD;
    s_firstPause = true;
    s_owed = true;
    return 3;
}
BOOL GetThreadContext(HANDLE, CONTEXT* context)
{
    Record("context");
    context->Eip = 0x12345678;
    context->FloatSave.ControlWord = 0x27f;
    s_lastError = 222;
    return !Scenario("context");
}
DWORD ResumeThread(HANDLE)
{
    Record("resume");
    s_firstPause = false;
    ++s_resumes;
    s_lastError = 333;
    if (((Scenario("resume_once") || Scenario("resume_flush_error")) && s_resumes == 1) || Scenario("target_exit"))
        return MAXDWORD;
    s_owed = false;
    return 4;
}
BOOL GetExitCodeThread(HANDLE, DWORD* code)
{
    Record("exit_code");
    *code = Scenario("target_exit") ? 0 : STILL_ACTIVE;
    if (*code != STILL_ACTIVE)
        s_owed = false;
    return 1;
}
extern "C" int __wrap_fflush(FILE* file)
{
    CheckIo();
    const int result = __real_fflush(file);
    return Scenario("resume_flush_error") && s_resumes == 1 ? EOF : result;
}
extern "C" int __wrap_fclose(FILE* file)
{
    CheckIo();
    Record("fclose");
    const int result = __real_fclose(file);
    return Scenario("fclose") ? EOF : result;
}
extern "C" int __wrap_printf(const char* format, ...)
{
    CheckIo();
    va_list args;
    va_start(args, format);
    const int result = std::vprintf(format, args);
    va_end(args);
    return result;
}
extern "C" int __wrap_puts(const char* text)
{
    CheckIo();
    return std::fputs(text, stdout) < 0 || std::fputc('\n', stdout) == EOF ? EOF : 0;
}
