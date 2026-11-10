#include "Core/Crash/CrashHandler.h"

#include "Core/Crash/CrashDump.h"
#include "Core/Crash/CrashFrames.h"
#include "Core/Crash/CrashReport.h"
#include "Core/Crash/CrashTexts.h"
#include "Core/Crash/Crc32.h"
#include "Core/Crash/IMemoryReader.h"
#include "Core/Crash/MemoryTotals.h"
#include "Core/Crash/Registers64.h"
#include "Core/Expected.h"
#include "Core/PathText.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#if defined(_WIN32)
// Sin las macros min y max de <windows.h>, que pisan std::min, ni lo que el manejador no necesita.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// Después de <windows.h>, que define lo que usa.
#include <tlhelp32.h>

#include <intrin.h>
#elif defined(__linux__)
#include "Core/Crash/ProcSelfFiles.h"
#include "Core/Crash/SignalCodes.h"

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <linux/sysinfo.h>
#include <signal.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/ucontext.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#if defined(__x86_64__)
#include <cpuid.h>
#include <x86intrin.h>
#endif
#endif

namespace Client::Core::Crash
{
namespace
{
using PathChar = std::filesystem::path::value_type;
// Una ruta copiada al instalar, terminada en 0.
using StoredPath = std::array<PathChar, MaxCrashPathLength + 1>;

// El fin de línea del FileSink: su ofstream es de texto, y en Windows escribe "\n" como "\r\n", el
// mismo fin que el mensaje del original (0x6c80b4).
#if defined(_WIN32)
constexpr std::string_view LineEnd = "\r\n";
#else
constexpr std::string_view LineEnd = "\n";
#endif

// Lo que mide la muestra de la velocidad del procesador: el Sleep(0x32) de 0x684d40.
constexpr unsigned SampleMilliseconds = 50;
// El bit 4 de EDX en la hoja 1 de cpuid: hay TSC (0x684ee1). Sin él, 0x684ed0 da 0 MHz.
constexpr std::uint32_t CpuidTscBit = 0x10;
// Para leer /proc/self/maps y el ejecutable de a pedazos.
constexpr std::size_t ReadBufferSize = 256 * 1024;
constexpr std::size_t MaxLogLineSize = 128;

#if defined(__linux__)
// Las señales de una caída: las que en Windows serían una excepción sin manejar. SIGABRT no, como
// en el original: abort() de la CRT no pasa por el filtro.
constexpr std::array HandledSignals{SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP};
// La pila aparte, para que un desborde de la pila principal también llegue al manejador.
constexpr std::size_t AlternateStackSize = 64 * 1024;
#endif

// Todo lo que usa el manejador después de la caída, armado antes: no puede pedir memoria.
struct CrashState
{
    std::atomic<bool> installed{false};
    // El primer hilo que cae lo toma; otro que caiga a la vez espera a que el proceso termine.
    std::atomic_flag handling;
    StoredPath dumpPath{};
    StoredPath logPath{};
    // Se apaga mientras SetLogFile copia la ruta, así el manejador no lee una a medias.
    std::atomic<bool> hasLogPath{false};
    std::array<char, MaxLogLineSize> logLine{};
    std::size_t logLineLength = 0;
    CrashReport report;
    std::array<std::byte, MaxCrashDumpSize> dump{};
    std::array<char, ReadBufferSize> readBuffer{};
#if defined(_WIN32)
    LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
#elif defined(__linux__)
    // Hecho una vez al instalar: uname no está en la lista de funciones seguras en una señal.
    CrashText systemText{};
    std::array<struct sigaction, HandledSignals.size()> previousActions{};
    stack_t previousStack{};
    alignas(16) std::array<std::byte, AlternateStackSize> alternateStack{};
    // El caño de PipeMemoryReader.
    int pipeRead = -1;
    int pipeWrite = -1;
#endif
};

CrashState s_state;

std::uint32_t Low32(std::uint64_t value)
{
    return static_cast<std::uint32_t>(value);
}

bool CopyPath(StoredPath& target, const std::filesystem::path& path)
{
    const auto& native = path.native();
    if (native.size() > MaxCrashPathLength)
    {
        return false;
    }
    target.fill(PathChar{});
    std::copy(native.begin(), native.end(), target.begin());
    return true;
}

std::span<const std::byte> LogLineBytes()
{
    return std::as_bytes(std::span(s_state.logLine).first(s_state.logLineLength));
}

// Lo que 0x4b9a70 hace con el reporte ya armado: el archivo y, si se escribió entero, la línea
// del log. Declaradas acá y definidas por sistema.
bool WriteNewFile(const PathChar* path, std::span<const std::byte> bytes);
void AppendToFile(const PathChar* path, std::span<const std::byte> bytes);

void WriteDumpAndLogLine()
{
    const std::size_t size = WriteCrashDump(s_state.report, s_state.dump);
    if (size == 0 || !WriteNewFile(s_state.dumpPath.data(), std::span(s_state.dump).first(size)))
    {
        return;
    }
    if (s_state.hasLogPath.load())
    {
        AppendToFile(s_state.logPath.data(), LogLineBytes());
    }
}

#if defined(_WIN32)
// ---- Windows: SetUnhandledExceptionFilter, como 0x67f710 ----

// IsBadReadPtr del original (0x68289b): ReadProcessMemory sobre el propio proceso devuelve FALSE
// en vez de una excepción si algo no se puede leer.
class ProcessMemoryReader final : public IMemoryReader
{
public:
    bool Read(std::uint64_t address, std::span<std::byte> out) const override
    {
        SIZE_T copied = 0;
        const auto* source = reinterpret_cast<LPCVOID>(static_cast<std::uintptr_t>(address));
        return ReadProcessMemory(GetCurrentProcess(), source, out.data(), out.size(), &copied) != FALSE &&
               copied == out.size();
    }
};

bool WriteNewFile(const PathChar* path, std::span<const std::byte> bytes)
{
    // El fopen(..., "wb") de 0x67f89b: crea o vacía, compartido.
    const HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    // El original no mira sus fwrite (0x67ff67) y da por escrito un archivo que solo abrió;
    // MuRet pide que entre entero (Q-C245).
    DWORD written = 0;
    const bool wrote = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE &&
                       written == bytes.size();
    const bool closed = CloseHandle(file) != FALSE;
    return wrote && closed;
}

void AppendToFile(const PathChar* path, std::span<const std::byte> bytes)
{
    // El FileSink lo tiene abierto compartido: otro handle que agrega escribe después de su última
    // línea, que ya pasó por su flush.
    const HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }
    DWORD written = 0;
    static_cast<void>(WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr));
    CloseHandle(file);
}

// El texto de 0x683ed0. GetVersionExA está marcada como obsoleta, y es la que usa el original:
// en Windows 8.1 o más nuevo, sin un manifiesto que diga otra cosa, contesta 6.2 ("Unknown 6.2"),
// como al original. El ProductType del registro solo cuenta con 5.0 (Windows 2000), que no corre
// MuRet de 64 bits: no se lee.
CrashText ReadSystemText()
{
    OSVERSIONINFOA info{};
    info.dwOSVersionInfoSize = static_cast<DWORD>(sizeof(info));
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const BOOL answered = GetVersionExA(&info);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (answered == FALSE)
    {
        return SystemText(OsVersion{});
    }
    return SystemText(OsVersion{.major = info.dwMajorVersion,
                                .minor = info.dwMinorVersion,
                                .build = info.dwBuildNumber,
                                .platformId = info.dwPlatformId,
                                .servicePack = std::string_view(info.szCSDVersion),
                                .productType = {}});
}

CpuId ReadCpuId(bool& hasTsc)
{
    constexpr int VendorLeaf = 0;
    constexpr int SignatureLeaf = 1;
    std::array<int, 4> registers{};
    __cpuid(registers.data(), VendorLeaf);
    CpuId cpu{.vendorEbx = static_cast<std::uint32_t>(registers[1]),
              .vendorEdx = static_cast<std::uint32_t>(registers[3]),
              .vendorEcx = static_cast<std::uint32_t>(registers[2]),
              .signature = 0};
    __cpuid(registers.data(), SignatureLeaf);
    cpu.signature = static_cast<std::uint32_t>(registers[0]);
    hasTsc = (static_cast<std::uint32_t>(registers[3]) & CpuidTscBit) != 0;
    return cpu;
}

// 0x684ed0: con la prioridad más alta y en el procesador 0, los ciclos del TSC durante 50 ms de
// QueryPerformanceCounter. Después deja todo como estaba.
std::int64_t SampleMegahertz()
{
    LARGE_INTEGER frequency{};
    if (QueryPerformanceFrequency(&frequency) == FALSE)
    {
        return 0;
    }
    const HANDLE process = GetCurrentProcess();
    const HANDLE thread = GetCurrentThread();
    const DWORD priorityClass = GetPriorityClass(process);
    const int threadPriority = GetThreadPriority(thread);
    DWORD_PTR processMask = 0;
    DWORD_PTR systemMask = 0;
    static_cast<void>(GetProcessAffinityMask(process, &processMask, &systemMask));
    static_cast<void>(SetPriorityClass(process, REALTIME_PRIORITY_CLASS));
    static_cast<void>(SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL));
    static_cast<void>(SetProcessAffinityMask(process, 1));
    LARGE_INTEGER start{};
    LARGE_INTEGER end{};
    QueryPerformanceCounter(&start);
    const std::uint64_t startCycles = __rdtsc();
    Sleep(SampleMilliseconds);
    QueryPerformanceCounter(&end);
    const std::uint64_t endCycles = __rdtsc();
    static_cast<void>(SetProcessAffinityMask(process, processMask));
    static_cast<void>(SetThreadPriority(thread, threadPriority));
    static_cast<void>(SetPriorityClass(process, priorityClass));
    return MegahertzFromSample(endCycles - startCycles, end.QuadPart - start.QuadPart, frequency.QuadPart);
}

CrashText ReadProcessorText()
{
    bool hasTsc = false;
    const CpuId cpu = ReadCpuId(hasTsc);
    return ProcessorText(cpu, hasTsc ? SampleMegahertz() : 0);
}

// GlobalMemoryStatus (0x67f9b0), en 64 bits.
MemoryStatus32 ReadMemory()
{
    MEMORYSTATUSEX status{};
    status.dwLength = static_cast<DWORD>(sizeof(status));
    if (GlobalMemoryStatusEx(&status) == FALSE)
    {
        return {};
    }
    const MemoryTotals totals{.totalPhysical = status.ullTotalPhys,
                              .availablePhysical = status.ullAvailPhys,
                              .totalPageFile = status.ullTotalPageFile,
                              .availablePageFile = status.ullAvailPageFile,
                              .totalVirtual = status.ullTotalVirtual,
                              .availableVirtual = status.ullAvailVirtual};
    return ToMemoryStatus32(totals, status.dwMemoryLoad);
}

// Un nombre de toolhelp en la página ANSI, como lo da la versión A que usa el original. MuRet pide
// la W, que no depende de que UNICODE esté definido, y la pasa con CP_ACP: los mismos bytes.
template <std::size_t Size> void CopyAnsiText(std::array<char, Size>& field, const wchar_t* text)
{
    constexpr std::size_t AnsiBufferSize = 2 * MAX_PATH;
    std::array<char, AnsiBufferSize> ansi{};
    const int written =
        WideCharToMultiByte(CP_ACP, 0, text, -1, ansi.data(), static_cast<int>(ansi.size()), nullptr, nullptr);
    CopyCrashText(field, written > 0 ? std::string_view(ansi.data()) : std::string_view());
}

// El recorrido de los procesos de 0x67f9c9..0x67fa42: el propio, por su id, da el nombre del
// ejecutable y los hilos.
void ReadProcessEntry(CrashReport& report)
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = static_cast<DWORD>(sizeof(entry));
    for (BOOL more = Process32FirstW(snapshot, &entry); more != FALSE; more = Process32NextW(snapshot, &entry))
    {
        if (entry.th32ProcessID == report.processId)
        {
            CopyAnsiText(report.executable, entry.szExeFile);
            report.threadCount = entry.cntThreads;
            break;
        }
    }
    CloseHandle(snapshot);
}

// 0x683bd0 sobre el ejecutable. El original abre el nombre del proceso, relativo a la carpeta
// actual; MuRet, su ruta entera (Q-C243).
std::optional<std::uint32_t> ExecutableCrc()
{
    std::array<wchar_t, MaxCrashPathLength> path{};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
    {
        return std::nullopt;
    }
    const HANDLE file =
        CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return std::nullopt;
    }
    Crc32 crc;
    bool failed = false;
    for (;;)
    {
        DWORD count = 0;
        if (ReadFile(file, s_state.readBuffer.data(), static_cast<DWORD>(s_state.readBuffer.size()), &count, nullptr) ==
            FALSE)
        {
            failed = true;
            break;
        }
        if (count == 0)
        {
            break;
        }
        crc.Update(std::as_bytes(std::span(s_state.readBuffer).first(count)));
    }
    CloseHandle(file);
    if (failed)
    {
        return std::nullopt;
    }
    return crc.Value();
}

// Los módulos de 0x67fdb9..0x67feb1, hasta 48.
std::uint32_t ReadModules(std::uint32_t processId, std::span<CrashModule, MaxCrashModules> modules)
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, processId);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return 0;
    }
    MODULEENTRY32W entry{};
    entry.dwSize = static_cast<DWORD>(sizeof(entry));
    std::uint32_t count = 0;
    for (BOOL more = Module32FirstW(snapshot, &entry); more != FALSE && count < modules.size();
         more = Module32NextW(snapshot, &entry))
    {
        CrashModule& module = modules[count++];
        CopyAnsiText(module.name, entry.szModule);
        CopyAnsiText(module.path, entry.szExePath);
        module.base = Low32(reinterpret_cast<std::uintptr_t>(entry.modBaseAddr));
        module.size = entry.modBaseSize;
    }
    CloseHandle(snapshot);
    return count;
}

Registers64 RegistersOf(const CONTEXT& context)
{
    Registers64 registers;
#if defined(_M_X64)
    registers.rax = context.Rax;
    registers.rbx = context.Rbx;
    registers.rcx = context.Rcx;
    registers.rdx = context.Rdx;
    registers.rsi = context.Rsi;
    registers.rdi = context.Rdi;
    registers.rbp = context.Rbp;
    registers.rsp = context.Rsp;
    registers.rip = context.Rip;
    registers.eflags = context.EFlags;
    registers.segCs = context.SegCs;
    registers.segDs = context.SegDs;
    registers.segEs = context.SegEs;
    registers.segFs = context.SegFs;
    registers.segGs = context.SegGs;
    registers.segSs = context.SegSs;
    constexpr auto DebugRegisters = static_cast<DWORD>(CONTEXT_DEBUG_REGISTERS);
    registers.hasDebugRegisters = (context.ContextFlags & DebugRegisters) == DebugRegisters;
    registers.debugRegisters = {context.Dr0, context.Dr1, context.Dr2, context.Dr3, context.Dr6, context.Dr7};
    registers.fxsave = std::as_bytes(std::span(&context.FltSave, 1));
#else
    // MuRet se compila para x64 (los presets de CMake). En otra arquitectura el CONTEXT va en 0.
    static_cast<void>(context);
#endif
    return registers;
}

// Lo que junta el filtro (0x680180) y 0x67f840, en el orden de la cabecera.
void FillReport(const EXCEPTION_POINTERS& pointers)
{
    CrashReport& report = s_state.report;
    report.time = static_cast<std::uint32_t>(std::time(nullptr));
    report.system = ReadSystemText();
    report.processor = ReadProcessorText();
    report.memory = ReadMemory();
    report.processId = GetCurrentProcessId();
    ReadProcessEntry(report);
    if (const auto crc = ExecutableCrc())
    {
        report.executableCrc = *crc;
    }
    // Main.exe no tiene recursos de versión: GetFileVersionInfoSizeA daría 0 (Q-C243).
    report.hasVersionInfo = false;
    report.moduleCount = ReadModules(report.processId, report.modules);
    const Registers64 registers = RegistersOf(*pointers.ContextRecord);
    report.context = ToX86Context(registers);
    const EXCEPTION_RECORD& record = *pointers.ExceptionRecord;
    report.exceptionCode = record.ExceptionCode;
    report.exceptionAddress = Low32(reinterpret_cast<std::uintptr_t>(record.ExceptionAddress));
    report.exceptionText =
        ExceptionText(record.ExceptionCode, record.ExceptionInformation[0], record.ExceptionInformation[1]);
    const ProcessMemoryReader memory{};
    report.frameCount =
        static_cast<std::uint32_t>(WalkCrashFrames(registers.rbp, sizeof(void*), memory, report.frames));
}

// El filtro: 0x680180 con 0x4b9a70 adentro. Devuelve EXCEPTION_EXECUTE_HANDLER, como el original
// con la bandera en 0 (0x680220): el proceso termina sin el diálogo del sistema.
LONG WINAPI HandleUnhandledException(EXCEPTION_POINTERS* pointers)
{
    if (s_state.handling.test_and_set())
    {
        // Otro hilo ya está escribiendo el volcado: el proceso termina cuando ese vuelva.
        Sleep(INFINITE);
    }
    if (pointers != nullptr && pointers->ExceptionRecord != nullptr && pointers->ContextRecord != nullptr)
    {
        FillReport(*pointers);
        WriteDumpAndLogLine();
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

Expected<void, std::string> InstallPlatform()
{
    s_state.previousFilter = SetUnhandledExceptionFilter(HandleUnhandledException);
    return {};
}

void UninstallPlatform()
{
    SetUnhandledExceptionFilter(s_state.previousFilter);
    s_state.previousFilter = nullptr;
}

#elif defined(__linux__)
// ---- Linux: sigaction, en una pila aparte ----

// IsBadReadPtr del original (0x68289b): write() desde una dirección que no se puede leer devuelve
// EFAULT en vez de una señal. Lo escrito en el caño se vuelve a leer en el momento.
class PipeMemoryReader final : public IMemoryReader
{
public:
    bool Read(std::uint64_t address, std::span<std::byte> out) const override
    {
        const auto* source = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address));
        const auto sent = write(s_state.pipeWrite, source, out.size());
        if (sent <= 0)
        {
            return false;
        }
        // Lo que entró se saca siempre, aunque falte una parte, para dejar el caño vacío.
        const auto received = read(s_state.pipeRead, out.data(), static_cast<std::size_t>(sent));
        return received == sent && static_cast<std::size_t>(sent) == out.size();
    }
};

bool WriteAll(int file, std::span<const std::byte> bytes)
{
    std::size_t written = 0;
    while (written < bytes.size())
    {
        const auto count = write(file, bytes.data() + written, bytes.size() - written);
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count <= 0)
        {
            return false;
        }
        written += static_cast<std::size_t>(count);
    }
    return true;
}

// Para los archivos nuevos: rw-r--r--, menos lo que saque la umask.
constexpr mode_t NewFileMode = 0644;

bool WriteNewFile(const PathChar* path, std::span<const std::byte> bytes)
{
    // El fopen(..., "wb") de 0x67f89b: crea o vacía.
    const int file = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, NewFileMode);
    if (file < 0)
    {
        return false;
    }
    // El original no mira sus fwrite (0x67ff67) y da por escrito un archivo que solo abrió;
    // MuRet pide que entre entero (Q-C245).
    const bool wrote = WriteAll(file, bytes);
    const bool closed = close(file) == 0;
    return wrote && closed;
}

void AppendToFile(const PathChar* path, std::span<const std::byte> bytes)
{
    // O_APPEND, como el FileSink: la línea va después de la última que escribió, ya con su flush.
    const int file = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, NewFileMode);
    if (file < 0)
    {
        return;
    }
    static_cast<void>(WriteAll(file, bytes));
    close(file);
}

// Lee `path` entero, hasta llenar el buffer de lectura. Devuelve lo que leyó.
std::string_view ReadSmallFile(const char* path)
{
    const int file = open(path, O_RDONLY | O_CLOEXEC);
    if (file < 0)
    {
        return {};
    }
    std::size_t total = 0;
    while (total < s_state.readBuffer.size())
    {
        const auto count = read(file, s_state.readBuffer.data() + total, s_state.readBuffer.size() - total);
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count <= 0)
        {
            break;
        }
        total += static_cast<std::size_t>(count);
    }
    close(file);
    return {s_state.readBuffer.data(), total};
}

std::optional<std::uint32_t> FileCrc(const char* path)
{
    const int file = open(path, O_RDONLY | O_CLOEXEC);
    if (file < 0)
    {
        return std::nullopt;
    }
    Crc32 crc;
    bool failed = false;
    for (;;)
    {
        const auto count = read(file, s_state.readBuffer.data(), s_state.readBuffer.size());
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count < 0)
        {
            failed = true;
        }
        if (count <= 0)
        {
            break;
        }
        crc.Update(std::as_bytes(std::span(s_state.readBuffer).first(static_cast<std::size_t>(count))));
    }
    close(file);
    if (failed)
    {
        return std::nullopt;
    }
    return crc.Value();
}

#if defined(__x86_64__)
CpuId ReadCpuId(bool& hasTsc)
{
    constexpr unsigned VendorLeaf = 0;
    constexpr unsigned SignatureLeaf = 1;
    unsigned eax = 0;
    unsigned ebx = 0;
    unsigned ecx = 0;
    unsigned edx = 0;
    CpuId cpu;
    if (__get_cpuid(VendorLeaf, &eax, &ebx, &ecx, &edx) != 0)
    {
        cpu.vendorEbx = ebx;
        cpu.vendorEdx = edx;
        cpu.vendorEcx = ecx;
    }
    if (__get_cpuid(SignatureLeaf, &eax, &ebx, &ecx, &edx) != 0)
    {
        cpu.signature = eax;
        hasTsc = (edx & CpuidTscBit) != 0;
    }
    return cpu;
}

std::int64_t Nanoseconds(const timespec& time)
{
    constexpr std::int64_t NanosecondsPerSecond = 1000000000;
    return static_cast<std::int64_t>(time.tv_sec) * NanosecondsPerSecond + time.tv_nsec;
}

// 0x684ed0 con un reloj de Linux en vez de QueryPerformanceCounter, y sin cambiar la prioridad ni
// el procesador: sched_setaffinity no es segura dentro de una señal.
std::int64_t SampleMegahertz()
{
    constexpr std::int64_t NanosecondsPerSecond = 1000000000;
    constexpr long NanosecondsPerMillisecond = 1000000;
    timespec start{};
    timespec end{};
    const timespec sleep{.tv_sec = 0, .tv_nsec = static_cast<long>(SampleMilliseconds) * NanosecondsPerMillisecond};
    clock_gettime(CLOCK_MONOTONIC, &start);
    const std::uint64_t startCycles = __rdtsc();
    nanosleep(&sleep, nullptr);
    clock_gettime(CLOCK_MONOTONIC, &end);
    const std::uint64_t endCycles = __rdtsc();
    return MegahertzFromSample(endCycles - startCycles, Nanoseconds(end) - Nanoseconds(start), NanosecondsPerSecond);
}

CrashText ReadProcessorText()
{
    bool hasTsc = false;
    const CpuId cpu = ReadCpuId(hasTsc);
    return ProcessorText(cpu, hasTsc ? SampleMegahertz() : 0);
}

// El bit 1 del código de error de una falla de página: fue una escritura.
constexpr std::uint64_t PageFaultWrite = 0x2;

Registers64 RegistersOf(const ucontext_t& context)
{
    const auto value = [&context](int index) { return static_cast<std::uint64_t>(context.uc_mcontext.gregs[index]); };
    constexpr unsigned SegmentBits = 16;
    Registers64 registers;
    registers.rax = value(REG_RAX);
    registers.rbx = value(REG_RBX);
    registers.rcx = value(REG_RCX);
    registers.rdx = value(REG_RDX);
    registers.rsi = value(REG_RSI);
    registers.rdi = value(REG_RDI);
    registers.rbp = value(REG_RBP);
    registers.rsp = value(REG_RSP);
    registers.rip = value(REG_RIP);
    registers.eflags = static_cast<std::uint32_t>(value(REG_EFL));
    // cs, gs y fs de a 16 bits, y ss en los de arriba. ds y es no están en x86-64: quedan en 0.
    const std::uint64_t segments = value(REG_CSGSFS);
    registers.segCs = static_cast<std::uint16_t>(segments);
    registers.segGs = static_cast<std::uint16_t>(segments >> SegmentBits);
    registers.segFs = static_cast<std::uint16_t>(segments >> (2 * SegmentBits));
    registers.segSs = static_cast<std::uint16_t>(segments >> (3 * SegmentBits));
    if (context.uc_mcontext.fpregs != nullptr)
    {
        registers.fxsave = std::as_bytes(std::span(context.uc_mcontext.fpregs, 1));
    }
    return registers;
}

// ExceptionInformation[0] de un acceso inválido: 0 al leer, 1 al escribir.
std::uint64_t AccessKind(int signal, const ucontext_t& context)
{
    const auto error = static_cast<std::uint64_t>(context.uc_mcontext.gregs[REG_ERR]);
    return signal == SIGSEGV && (error & PageFaultWrite) != 0 ? 1 : 0;
}
#else
// Otra arquitectura: sin cpuid ni los registros de x86-64.
CrashText ReadProcessorText()
{
    return ProcessorText(CpuId{}, 0);
}

Registers64 RegistersOf(const ucontext_t& /*context*/)
{
    return {};
}

std::uint64_t AccessKind(int /*signal*/, const ucontext_t& /*context*/)
{
    return 0;
}
#endif

// GlobalMemoryStatus con sysinfo: la física libre más los buffers, y la de "page file" con la
// swap sumada. El espacio de direcciones de un proceso de 64 bits (128 TiB en x86-64) no entra en
// 32 bits: queda en 0xFFFFFFFF, como en Windows de 64 bits (Q-C242).
MemoryStatus32 ReadMemory()
{
    constexpr std::uint64_t AddressSpace = std::uint64_t{1} << 47;
    struct sysinfo info
    {
    };
    if (sysinfo(&info) != 0)
    {
        return {};
    }
    const std::uint64_t unit = info.mem_unit;
    const std::uint64_t totalPhysical = info.totalram * unit;
    const std::uint64_t availablePhysical = (info.freeram + info.bufferram) * unit;
    const MemoryTotals totals{.totalPhysical = totalPhysical,
                              .availablePhysical = availablePhysical,
                              .totalPageFile = totalPhysical + info.totalswap * unit,
                              .availablePageFile = availablePhysical + info.freeswap * unit,
                              .totalVirtual = AddressSpace,
                              .availableVirtual = AddressSpace};
    return ToMemoryStatus32(totals, MemoryLoadPercent(totalPhysical, availablePhysical));
}

// El nombre del ejecutable, el de su ruta en /proc/self/exe, y su CRC.
void ReadExecutable(CrashReport& report)
{
    constexpr const char* SelfExe = "/proc/self/exe";
    std::array<char, MaxCrashPathLength> path{};
    const auto length = readlink(SelfExe, path.data(), path.size());
    if (length > 0)
    {
        const std::string_view text(path.data(), static_cast<std::size_t>(length));
        CopyCrashText(report.executable, text.substr(text.rfind('/') + 1));
    }
    if (const auto crc = FileCrc(SelfExe))
    {
        report.executableCrc = *crc;
    }
}

void FillReport(int signal, const siginfo_t& info, const ucontext_t* context)
{
    CrashReport& report = s_state.report;
    report.time = static_cast<std::uint32_t>(std::time(nullptr));
    report.system = s_state.systemText;
    report.processor = ReadProcessorText();
    report.memory = ReadMemory();
    report.processId = static_cast<std::uint32_t>(getpid());
    ReadExecutable(report);
    report.threadCount = ThreadCountFromProcStat(ReadSmallFile("/proc/self/stat")).value_or(0);
    // Linux no tiene recursos de versión (Q-C243).
    report.hasVersionInfo = false;
    report.moduleCount =
        static_cast<std::uint32_t>(ModulesFromProcMaps(ReadSmallFile("/proc/self/maps"), report.modules));
    const Registers64 registers = context != nullptr ? RegistersOf(*context) : Registers64{};
    report.context = ToX86Context(registers);
    report.exceptionCode = ExceptionCodeForSignal(signal, info.si_code);
    report.exceptionAddress = Low32(registers.rip);
    // La dirección de la falla, si la señal es del procesador: la de raise o kill no tiene una.
    const std::uint64_t faultAddress =
        info.si_code > 0 ? static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(info.si_addr)) : 0;
    const std::uint64_t accessKind = context != nullptr ? AccessKind(signal, *context) : 0;
    report.exceptionText = ExceptionText(report.exceptionCode, accessKind, faultAddress);
    const PipeMemoryReader memory{};
    report.frameCount =
        static_cast<std::uint32_t>(WalkCrashFrames(registers.rbp, sizeof(void*), memory, report.frames));
}

void HandleSignal(int signal, siginfo_t* info, void* context)
{
    if (s_state.handling.test_and_set())
    {
        // Otro hilo ya está escribiendo el volcado: este espera a que el proceso termine.
        for (;;)
        {
            pause();
        }
    }
    if (info != nullptr)
    {
        FillReport(signal, *info, static_cast<const ucontext_t*>(context));
        WriteDumpAndLogLine();
    }
    // La acción por defecto y la señal otra vez: el proceso muere por ella, como sin manejador. La
    // del procesador vuelve a darse al volver de acá; la de raise queda pendiente hasta entonces,
    // porque mientras corre el manejador está bloqueada.
    struct sigaction defaultAction
    {
    };
    defaultAction.sa_handler = SIG_DFL;
    sigemptyset(&defaultAction.sa_mask);
    sigaction(signal, &defaultAction, nullptr);
    raise(signal);
}

void ClosePipe()
{
    for (int* end : std::array{&s_state.pipeRead, &s_state.pipeWrite})
    {
        if (*end >= 0)
        {
            close(*end);
            *end = -1;
        }
    }
}

Expected<void, std::string> InstallPlatform()
{
    std::array<int, 2> ends{};
    if (pipe2(ends.data(), O_CLOEXEC | O_NONBLOCK) != 0)
    {
        return Unexpected(std::string("no se pudo abrir el caño del manejador de caídas"));
    }
    s_state.pipeRead = ends[0];
    s_state.pipeWrite = ends[1];
    utsname names{};
    s_state.systemText = uname(&names) == 0 ? PosixSystemText(names.sysname, names.release) : CrashText{};

    stack_t stack{};
    stack.ss_sp = s_state.alternateStack.data();
    stack.ss_size = s_state.alternateStack.size();
    if (sigaltstack(&stack, &s_state.previousStack) != 0)
    {
        ClosePipe();
        return Unexpected(std::string("no se pudo poner la pila aparte del manejador de caídas"));
    }
    struct sigaction action
    {
    };
    action.sa_sigaction = HandleSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (std::size_t i = 0; i < HandledSignals.size(); ++i)
    {
        sigaction(HandledSignals[i], &action, &s_state.previousActions[i]);
    }
    return {};
}

void UninstallPlatform()
{
    for (std::size_t i = 0; i < HandledSignals.size(); ++i)
    {
        sigaction(HandledSignals[i], &s_state.previousActions[i], nullptr);
    }
    sigaltstack(&s_state.previousStack, nullptr);
    ClosePipe();
}

#else
// ---- Otro sistema: sin manejador ----

bool WriteNewFile(const PathChar* /*path*/, std::span<const std::byte> /*bytes*/)
{
    return false;
}

void AppendToFile(const PathChar* /*path*/, std::span<const std::byte> /*bytes*/) {}

Expected<void, std::string> InstallPlatform()
{
    return Unexpected(std::string("este sistema no tiene manejador de caídas"));
}

void UninstallPlatform() {}
#endif
} // namespace

Expected<std::unique_ptr<CrashHandler>, std::string> CrashHandler::Install(const std::filesystem::path& dumpPath)
{
    if (s_state.installed.load())
    {
        return Unexpected(std::string("ya hay un manejador de caídas instalado"));
    }
    if (!CopyPath(s_state.dumpPath, dumpPath))
    {
        return Unexpected("la ruta del volcado es demasiado larga: " + PathText(dumpPath));
    }
    // La línea, armada ahora: después de la caída no se puede pedir memoria.
    const std::string line = CrashLogLine().append(LineEnd);
    s_state.logLineLength = std::min(line.size(), s_state.logLine.size());
    std::copy_n(line.begin(), s_state.logLineLength, s_state.logLine.begin());
    s_state.hasLogPath.store(false);
    const Expected<void, std::string> installed = InstallPlatform();
    if (!installed.HasValue())
    {
        return Unexpected(installed.Error());
    }
    s_state.installed.store(true);
    // Con new y no make_unique: el constructor es privado.
    return std::unique_ptr<CrashHandler>(new CrashHandler());
}

CrashHandler::~CrashHandler()
{
    UninstallPlatform();
    s_state.hasLogPath.store(false);
    s_state.installed.store(false);
}

bool CrashHandler::SetLogFile(const std::filesystem::path& logPath)
{
    s_state.hasLogPath.store(false);
    if (!CopyPath(s_state.logPath, logPath))
    {
        return false;
    }
    s_state.hasLogPath.store(true);
    return true;
}

bool CrashHandler::IsInstalled()
{
    return s_state.installed.load();
}
} // namespace Client::Core::Crash
