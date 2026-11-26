#include "support/GoldenDir.h"

#include "Core/Ascii.h"
#include "Core/Environment.h"
#include "Data/DataPath.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace
{
constexpr const char* GoldenDirVariable = "MU_CLIENT_GOLDEN_DIR";
constexpr const char* MemoryDirVariable = "MU_CLIENT_MEMORY_DIR";
constexpr const char* WireDirVariable = "MU_CLIENT_WIRE_DIR";
constexpr const char* FontFileVariable = "MU_CLIENT_FONT_FILE";
constexpr const char* GlTraceDirVariable = "MU_CLIENT_GL_TRACE_DIR";
} // namespace

namespace Client::Tests
{
std::optional<std::filesystem::path> GoldenDirFromValue(const std::optional<std::string>& value)
{
    if (!value || value->empty())
    {
        return std::nullopt;
    }
    return std::filesystem::path(*value);
}

bool HasExtension(const std::filesystem::path& path, std::string_view extension)
{
    return Core::EqualsIgnoringAsciiCase(path.extension().string(), extension);
}

GoldenDirState CheckGoldenDir(const std::optional<std::filesystem::path>& dir)
{
    if (!dir)
    {
        return GoldenDirState::NotSet;
    }
    std::error_code error;
    return std::filesystem::is_directory(*dir, error) ? GoldenDirState::Ready : GoldenDirState::Missing;
}

std::filesystem::path RepoDataDir()
{
    return std::filesystem::path(MU_CLIENT_REPO_DATA_DIR);
}

std::filesystem::path GoldenDirOrRepoData(const std::optional<std::string>& value)
{
    return GoldenDirFromValue(value).value_or(RepoDataDir());
}

std::filesystem::path GoldenDir()
{
    return GoldenDirOrRepoData(Core::ReadEnvironmentVariable(GoldenDirVariable));
}

namespace
{
// The rule, for any of the folders (data, memory dumps, wire and traces). GTEST_SKIP() and FAIL() return from this
// function, and GoogleTest does not run the body of a test skipped or failed in its SetUp().
void RequireDirectory(const char* variable, std::filesystem::path& dir)
{
    const std::optional<std::filesystem::path> value = GoldenDirFromValue(Core::ReadEnvironmentVariable(variable));
    const GoldenDirState state = CheckGoldenDir(value);
    if (state == GoldenDirState::NotSet)
    {
        GTEST_SKIP() << variable << " no está definida o está vacía: test salteado";
    }
    if (state == GoldenDirState::Missing)
    {
        FAIL() << variable << " apunta a " << *value << ", que no es una carpeta";
    }
    dir = *value;
}

// The file `name` in folder `root` (the wire's or the traces', per `what`). If it is missing, fails the test.
std::optional<std::filesystem::path> FindFile(const std::filesystem::path& root, std::string_view name,
                                              std::string_view what)
{
    std::error_code error;
    const std::filesystem::path path = root / name;
    if (!std::filesystem::is_regular_file(path, error))
    {
        ADD_FAILURE() << "falta " << name << " en la carpeta " << what << " " << root;
        return std::nullopt;
    }
    return path;
}

std::optional<std::filesystem::path> FindWireFile(const std::filesystem::path& root, std::string_view name)
{
    return FindFile(root, name, "del cable");
}
} // namespace

void GoldenDataTest::SetUp()
{
    // Without the variable it is not skipped: it reads the repo's Data.
    const std::filesystem::path dir = GoldenDir();
    if (CheckGoldenDir(dir) == GoldenDirState::Missing)
    {
        FAIL() << "la carpeta golden " << dir << " no es una carpeta (" << GoldenDirVariable
               << ", o la Data del repo si no está)";
    }
    m_dataRoot = dir;
}

std::optional<std::filesystem::path> GoldenDataTest::DataFile(std::string_view originalPath) const
{
    std::error_code error;
    std::optional<std::filesystem::path> path = Data::ResolveDataPath(m_dataRoot, originalPath, error);
    if (error)
    {
        ADD_FAILURE() << "no se pudo buscar " << originalPath << " en la carpeta golden " << m_dataRoot << ": "
                      << error.message();
    }
    else if (!path)
    {
        ADD_FAILURE() << "falta " << originalPath << " en la carpeta golden " << m_dataRoot;
    }
    return path;
}

DumpState CheckDump(const std::filesystem::path& root, std::string_view folder, std::string_view name)
{
    std::error_code error;
    if (std::filesystem::is_regular_file(root / folder / name, error))
    {
        return DumpState::Found;
    }
    if (std::filesystem::is_regular_file(root / name, error))
    {
        return DumpState::RootIsACapture;
    }
    return DumpState::Missing;
}

void MemoryDumpTest::SetUp()
{
    GoldenDataTest::SetUp();
    if (IsSkipped() || HasFatalFailure())
    {
        return;
    }
    RequireDirectory(MemoryDirVariable, m_dumpRoot);
}

std::optional<std::filesystem::path> MemoryDumpTest::DumpFile(const MemoryCapture& capture, std::string_view name) const
{
    switch (CheckDump(m_dumpRoot, capture.folder, name))
    {
    case DumpState::Found:
        return m_dumpRoot / capture.folder / name;
    case DumpState::RootIsACapture:
        ADD_FAILURE() << MemoryDirVariable << " apunta a " << m_dumpRoot << ", que es una captura: tiene que apuntar "
                      << "a la carpeta que las contiene, memoria-cliente";
        return std::nullopt;
    case DumpState::Missing:
        break;
    }
    ADD_FAILURE() << "falta " << name << " en la captura " << capture.folder << " de " << m_dumpRoot;
    return std::nullopt;
}

void WireDataTest::SetUp()
{
    RequireDirectory(WireDirVariable, m_wireRoot);
}

std::optional<std::filesystem::path> WireDataTest::WireFile(std::string_view name) const
{
    return FindWireFile(m_wireRoot, name);
}

void GoldenAndWireDataTest::SetUp()
{
    GoldenDataTest::SetUp();
    if (IsSkipped() || HasFatalFailure())
    {
        return;
    }
    RequireDirectory(WireDirVariable, m_wireRoot);
}

std::optional<std::filesystem::path> GoldenAndWireDataTest::WireFile(std::string_view name) const
{
    return FindWireFile(m_wireRoot, name);
}

void GoldenAndGlTraceTest::SetUp()
{
    GoldenDataTest::SetUp();
    if (IsSkipped() || HasFatalFailure())
    {
        return;
    }
    RequireDirectory(GlTraceDirVariable, m_traceRoot);
}

std::optional<std::filesystem::path> GoldenAndGlTraceTest::TraceFile(std::string_view name) const
{
    return FindFile(m_traceRoot, name, "de las trazas");
}

void FontFileTest::SetUp()
{
    const std::optional<std::filesystem::path> value =
        GoldenDirFromValue(Core::ReadEnvironmentVariable(FontFileVariable));
    if (!value)
    {
        GTEST_SKIP() << FontFileVariable << " no está definida o está vacía: test salteado";
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(*value, error))
    {
        FAIL() << FontFileVariable << " apunta a " << *value << ", que no es un archivo";
    }
    m_fontFile = *value;
}
} // namespace Client::Tests
