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
constexpr const char* GoldenDirVariable = "MURET_CLIENT_GOLDEN_DIR";
constexpr const char* MemoryDirVariable = "MURET_CLIENT_MEMORY_DIR";
constexpr const char* WireDirVariable = "MURET_CLIENT_WIRE_DIR";
constexpr const char* FontFileVariable = "MURET_CLIENT_FONT_FILE";
constexpr const char* GlTraceDirVariable = "MURET_CLIENT_GL_TRACE_DIR";
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

std::optional<std::filesystem::path> GoldenDir()
{
    return GoldenDirFromValue(Core::ReadEnvironmentVariable(GoldenDirVariable));
}

namespace
{
// La regla, para cualquiera de las carpetas (la de los datos, la de los volcados de memoria, la del
// cable y la de las trazas). GTEST_SKIP() y FAIL() salen de esta función, y GoogleTest no corre el
// cuerpo de un test que quedó salteado o fallado en su SetUp().
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

// El archivo `name` de la carpeta `root` (la del cable o la de las trazas, según `what`). Si no está,
// marca el test como fallado.
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
    RequireDirectory(GoldenDirVariable, m_dataRoot);
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
