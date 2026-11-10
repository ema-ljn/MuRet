#include "Core/Crash/CrashHandler.h"

#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#ifdef __linux__
#include "Core/Crash/CrashDump.h"
#include "Core/Crash/Crc32.h"
#include "Core/Crash/SignalCodes.h"
#include "support/FileBytes.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <csignal>
#include <ctime>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <signal.h>
#endif

// Client::Core::Crash::CrashHandler: instalar y desinstalar en el proceso del test, y en Linux una
// caída de verdad en un proceso hijo (EXPECT_EXIT), que deja MuError.dmp y la línea del log en una
// carpeta temporal. El proceso del test no se cae.
namespace
{
namespace fs = std::filesystem;
using Client::Core::Crash::CrashHandler;
using Client::Tests::TemporaryFolder;
} // namespace

// Uno solo a la vez: mientras hay uno, otro no se instala; al morir, se puede instalar de nuevo.
TEST(CrashHandlerTest, InstallsOneAtATime)
{
    const TemporaryFolder folder("crash-handler");
    ASSERT_FALSE(CrashHandler::IsInstalled());
    {
        const auto first = CrashHandler::Install(folder.Path() / "MuError.dmp");
        ASSERT_TRUE(first.HasValue()) << first.Error();
        EXPECT_TRUE(CrashHandler::IsInstalled());

        const auto second = CrashHandler::Install(folder.Path() / "MuError.dmp");
        ASSERT_FALSE(second.HasValue());
        EXPECT_EQ(second.Error(), "ya hay un manejador de caídas instalado");
        EXPECT_TRUE(CrashHandler::IsInstalled());
    }
    EXPECT_FALSE(CrashHandler::IsInstalled());

    const auto again = CrashHandler::Install(folder.Path() / "MuError.dmp");
    EXPECT_TRUE(again.HasValue());
}

// Las rutas van a un buffer fijo: una que no entra no se instala, y un log que no entra no se usa.
TEST(CrashHandlerTest, RejectsPathsThatDoNotFit)
{
    const fs::path tooLong(std::string(Client::Core::Crash::MaxCrashPathLength + 1, 'x'));

    const auto rejected = CrashHandler::Install(tooLong);
    ASSERT_FALSE(rejected.HasValue());
    EXPECT_FALSE(CrashHandler::IsInstalled());

    const auto handler = CrashHandler::Install("MuError.dmp");
    ASSERT_TRUE(handler.HasValue()) << handler.Error();
    EXPECT_FALSE(handler.Value()->SetLogFile(tooLong));
    EXPECT_TRUE(handler.Value()->SetLogFile("muret-client.log"));
}

#ifdef __linux__
namespace
{
using Client::Core::Crash::CrashFrameRecordSize;
using Client::Core::Crash::CrashHeaderSize;
using Client::Core::Crash::CrashModuleRecordSize;
using Client::Core::Crash::Crc32;
using Client::Core::Crash::ExceptionCodeForSignal;
using Client::Tests::ReadFileBytes;

// Offsets de la cabecera de MuError.dmp (test_crash_dump.cpp los prueba uno por uno).
constexpr std::size_t TimeOffset = 0x009;
constexpr std::size_t SystemOffset = 0x00d;
constexpr std::size_t ProcessorOffset = 0x08d;
constexpr std::size_t MemoryOffset = 0x10d;
constexpr std::size_t ExecutableOffset = 0x131;
constexpr std::size_t CrcOffset = 0x1b1;
constexpr std::size_t ThreadsOffset = 0x1b5;
constexpr std::size_t HasVersionOffset = 0x1b9;
constexpr std::size_t ModuleCountOffset = 0x1ba;
constexpr std::size_t ContextOffset = 0x1be;
constexpr std::size_t ExceptionCodeOffset = 0x48a;
constexpr std::size_t ExceptionTextOffset = 0x492;
constexpr std::size_t FrameCountOffset = 0x512;

constexpr std::string_view FirstLogLine = "[info] MuRet Client arranca\n";

std::uint32_t DwordAt(const std::vector<char>& bytes, std::size_t offset)
{
    constexpr unsigned BitsPerByte = 8;
    std::uint32_t value = 0;
    for (std::size_t i = 4; i > 0; --i)
    {
        value = (value << BitsPerByte) | static_cast<unsigned char>(bytes.at(offset + i - 1));
    }
    return value;
}

std::string_view TextAt(const std::vector<char>& bytes, std::size_t offset)
{
    const std::string_view all(bytes.data() + offset, bytes.size() - offset);
    return all.substr(0, all.find('\0'));
}

std::string FileText(const fs::path& path)
{
    const auto bytes = ReadFileBytes(path);
    return bytes ? std::string(bytes->begin(), bytes->end()) : std::string();
}

// Lo que corre en el hijo: instala, apunta el log si hay uno, y se cae con `signal`. Si algo de
// antes falla, sale con un código y el EXPECT_EXIT lo ve.
void CrashWith(int signal, const fs::path& dump, const std::optional<fs::path>& log)
{
    const auto handler = CrashHandler::Install(dump);
    if (!handler.HasValue())
    {
        std::_Exit(2);
    }
    if (log && !handler.Value()->SetLogFile(*log))
    {
        std::_Exit(3);
    }
    std::raise(signal);
    std::_Exit(4);
}

// Una carpeta con un log que ya tiene una línea, como el de MuRet después de arrancar.
class CrashHandlerDeathTest : public testing::Test
{
protected:
    CrashHandlerDeathTest() : m_folder("crash-death")
    {
        std::ofstream(LogPath(), std::ios::binary) << FirstLogLine;
    }

    [[nodiscard]] fs::path DumpPath() const
    {
        return m_folder.Path() / "MuError.dmp";
    }

    [[nodiscard]] fs::path LogPath() const
    {
        return m_folder.Path() / "muret-client.log";
    }

private:
    TemporaryFolder m_folder;
};
} // namespace

// Los códigos de Windows de cada señal, con el si_code del kernel o sin él (raise, kill).
TEST(CrashHandlerTest, MapsSignalsToWindowsExceptionCodes)
{
    EXPECT_EQ(ExceptionCodeForSignal(SIGSEGV, SEGV_MAPERR), 0xC0000005u);
    EXPECT_EQ(ExceptionCodeForSignal(SIGSEGV, SI_TKILL), 0xC0000005u);
    EXPECT_EQ(ExceptionCodeForSignal(SIGBUS, BUS_ADRERR), 0xC0000006u);
    EXPECT_EQ(ExceptionCodeForSignal(SIGILL, ILL_ILLOPC), 0xC000001Du);
    EXPECT_EQ(ExceptionCodeForSignal(SIGILL, ILL_PRVOPC), 0xC0000096u);
    EXPECT_EQ(ExceptionCodeForSignal(SIGILL, SI_TKILL), 0xC000001Du);
    EXPECT_EQ(ExceptionCodeForSignal(SIGFPE, FPE_INTDIV), 0xC0000094u);
    EXPECT_EQ(ExceptionCodeForSignal(SIGFPE, FPE_INTOVF), 0xC0000095u);
    EXPECT_EQ(ExceptionCodeForSignal(SIGFPE, FPE_FLTDIV), 0xC000008Eu);
    EXPECT_EQ(ExceptionCodeForSignal(SIGFPE, FPE_FLTINV), 0xC0000090u);
    EXPECT_EQ(ExceptionCodeForSignal(SIGFPE, SI_USER), 0xC0000094u);
    EXPECT_EQ(ExceptionCodeForSignal(SIGTRAP, TRAP_BRKPT), 0x80000003u);
    EXPECT_EQ(ExceptionCodeForSignal(SIGTRAP, TRAP_TRACE), 0x80000004u);
    EXPECT_EQ(ExceptionCodeForSignal(SIGABRT, SI_USER), 0u);
}

void NotTheCrashHandler(int /*signal*/) {}

// Instalar cambia la acción de las cinco señales y pone la pila aparte; desinstalar deja las de
// antes.
TEST(CrashHandlerTest, RestoresTheSignalActionsAndTheStack)
{
    struct sigaction previous
    {
    };
    previous.sa_handler = NotTheCrashHandler;
    sigemptyset(&previous.sa_mask);
    ASSERT_EQ(sigaction(SIGFPE, &previous, nullptr), 0);
    stack_t stackBefore{};
    ASSERT_EQ(sigaltstack(nullptr, &stackBefore), 0);
    {
        const auto handler = CrashHandler::Install("MuError.dmp");
        ASSERT_TRUE(handler.HasValue()) << handler.Error();
        for (const int signal : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP})
        {
            struct sigaction current
            {
            };
            ASSERT_EQ(sigaction(signal, nullptr, &current), 0);
            EXPECT_NE(current.sa_flags & SA_SIGINFO, 0) << signal;
            EXPECT_NE(current.sa_flags & SA_ONSTACK, 0) << signal;
        }
        stack_t stack{};
        ASSERT_EQ(sigaltstack(nullptr, &stack), 0);
        EXPECT_EQ(stack.ss_flags & SS_DISABLE, 0);
    }
    struct sigaction after
    {
    };
    ASSERT_EQ(sigaction(SIGFPE, nullptr, &after), 0);
    EXPECT_EQ(after.sa_handler, &NotTheCrashHandler);
    stack_t stackAfter{};
    ASSERT_EQ(sigaltstack(nullptr, &stackAfter), 0);
    EXPECT_EQ(stackAfter.ss_sp, stackBefore.ss_sp);
    EXPECT_EQ(stackAfter.ss_flags, stackBefore.ss_flags);

    struct sigaction defaultAction
    {
    };
    defaultAction.sa_handler = SIG_DFL;
    sigemptyset(&defaultAction.sa_mask);
    sigaction(SIGFPE, &defaultAction, nullptr);
}

// Un acceso inválido: el hijo muere por la señal, como sin manejador, y deja el volcado con lo que
// junta Linux y la línea al final del log.
TEST_F(CrashHandlerDeathTest, WritesTheDumpAndTheLogLineOnACrash)
{
    const auto before = static_cast<std::uint32_t>(std::time(nullptr));
    EXPECT_EXIT(CrashWith(SIGSEGV, DumpPath(), LogPath()), testing::KilledBySignal(SIGSEGV), "");

    const auto dump = ReadFileBytes(DumpPath());
    ASSERT_TRUE(dump.has_value());
    ASSERT_GE(dump->size(), CrashHeaderSize);
    EXPECT_EQ(TextAt(*dump, 0).substr(0, 4), "WDMP");
    EXPECT_EQ(dump->at(4), 2);
    EXPECT_EQ(DwordAt(*dump, 5), CrashHeaderSize);
    const std::uint32_t modules = DwordAt(*dump, ModuleCountOffset);
    const std::uint32_t frames = DwordAt(*dump, FrameCountOffset);
    EXPECT_EQ(dump->size(), CrashHeaderSize + modules * CrashModuleRecordSize + frames * CrashFrameRecordSize);

    const std::uint32_t time = DwordAt(*dump, TimeOffset);
    EXPECT_GE(time, before);
    EXPECT_LE(time, before + 60);
    EXPECT_EQ(TextAt(*dump, SystemOffset).substr(0, 6), "Linux ");
    EXPECT_NE(TextAt(*dump, ProcessorOffset).find(" CPU "), std::string_view::npos);
    EXPECT_EQ(DwordAt(*dump, MemoryOffset), 0x20u);
    EXPECT_LE(DwordAt(*dump, MemoryOffset + 4), 100u);

    // El ejecutable es este mismo test: su nombre y su CRC.
    const fs::path self = fs::read_symlink("/proc/self/exe");
    EXPECT_EQ(TextAt(*dump, ExecutableOffset), self.filename().string());
    const auto selfBytes = ReadFileBytes(self);
    ASSERT_TRUE(selfBytes.has_value());
    Crc32 crc;
    crc.Update(std::as_bytes(std::span(*selfBytes)));
    EXPECT_EQ(DwordAt(*dump, CrcOffset), crc.Value());
    EXPECT_GE(DwordAt(*dump, ThreadsOffset), 1u);
    EXPECT_EQ(dump->at(HasVersionOffset), 0);

    // El primer módulo es el ejecutable, el primer archivo de /proc/self/maps.
    ASSERT_GE(modules, 1u);
    ASSERT_LE(modules, 48u);
    EXPECT_EQ(TextAt(*dump, CrashHeaderSize), self.filename().string());
    EXPECT_LE(frames, 64u);
#ifdef __x86_64__
    // CONTEXT_i386 con CONTROL, INTEGER, SEGMENTS y la imagen de FXSAVE.
    EXPECT_EQ(DwordAt(*dump, ContextOffset), 0x00010027u);
#endif
    EXPECT_EQ(DwordAt(*dump, ExceptionCodeOffset), 0xC0000005u);
    EXPECT_EQ(TextAt(*dump, ExceptionTextOffset).substr(0, 19), "Access violation : ");

    EXPECT_EQ(FileText(LogPath()), std::string(FirstLogLine) + "[error] Save DumpFile complete - MuError.dmp\n");
}

// Sin SetLogFile no hay línea, como el original con el log cerrado; el volcado sale igual.
TEST_F(CrashHandlerDeathTest, WritesNoLogLineWithoutALogFile)
{
    EXPECT_EXIT(CrashWith(SIGFPE, DumpPath(), std::nullopt), testing::KilledBySignal(SIGFPE), "");

    const auto dump = ReadFileBytes(DumpPath());
    ASSERT_TRUE(dump.has_value());
    EXPECT_EQ(DwordAt(*dump, ExceptionCodeOffset), 0xC0000094u);
    EXPECT_EQ(TextAt(*dump, ExceptionTextOffset), "Int divide by zero");
    EXPECT_EQ(FileText(LogPath()), FirstLogLine);
}

// Si el volcado no se puede escribir entero, tampoco: el original abre, no mira sus fwrite y deja la
// línea igual (Q-C245). /dev/full abre y no deja escribir nada.
TEST_F(CrashHandlerDeathTest, WritesNoLogLineWhenTheDumpCannotBeWritten)
{
    EXPECT_EXIT(CrashWith(SIGSEGV, "/dev/full", LogPath()), testing::KilledBySignal(SIGSEGV), "");

    EXPECT_EQ(FileText(LogPath()), FirstLogLine);
}

// Si el volcado no abre (su carpeta no existe), tampoco hay línea: 0x4b9b27 la salta.
TEST_F(CrashHandlerDeathTest, WritesNoLogLineWhenTheDumpCannotBeOpened)
{
    const fs::path missing = DumpPath().parent_path() / "missing" / "MuError.dmp";

    EXPECT_EXIT(CrashWith(SIGILL, missing, LogPath()), testing::KilledBySignal(SIGILL), "");

    EXPECT_FALSE(fs::exists(missing));
    EXPECT_EQ(FileText(LogPath()), FirstLogLine);
}
#endif
