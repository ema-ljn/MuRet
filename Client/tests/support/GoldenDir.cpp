#include "support/GoldenDir.h"

#include "Data/DataPath.h"

#include <cstdlib>
#include <string>
#include <system_error>

namespace
{

constexpr const char* GoldenDirVariable = "MURET_CLIENT_GOLDEN_DIR";
constexpr const char* MemoryDirVariable = "MURET_CLIENT_MEMORY_DIR";

std::optional<std::string> ReadEnvironment(const char* name)
{
#ifdef _MSC_VER
    // getenv es "inseguro" para MSVC (C4996); _dupenv_s devuelve una copia que hay que liberar.
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr)
    {
        return std::nullopt;
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    if (value == nullptr)
    {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

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
    return GoldenDirFromValue(ReadEnvironment(GoldenDirVariable));
}

namespace
{

// La regla, para cualquiera de las dos variables. GTEST_SKIP() y FAIL() salen de esta función, y
// GoogleTest no corre el cuerpo de un test que quedó salteado o fallado en su SetUp().
void RequireDirectory(const char* variable, std::filesystem::path& dir)
{
    const std::optional<std::filesystem::path> value = GoldenDirFromValue(ReadEnvironment(variable));
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

} // namespace

void GoldenDataTest::SetUp()
{
    RequireDirectory(GoldenDirVariable, dataRoot);
}

std::optional<std::filesystem::path> GoldenDataTest::DataFile(std::string_view originalPath) const
{
    std::error_code error;
    std::optional<std::filesystem::path> path = Data::ResolveDataPath(dataRoot, originalPath, error);
    if (error)
    {
        ADD_FAILURE() << "no se pudo buscar " << originalPath << " en la carpeta golden " << dataRoot << ": "
                      << error.message();
    }
    else if (!path)
    {
        ADD_FAILURE() << "falta " << originalPath << " en la carpeta golden " << dataRoot;
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
    RequireDirectory(MemoryDirVariable, dumpRoot);
}

std::optional<std::filesystem::path> MemoryDumpTest::DumpFile(const MemoryCapture& capture, std::string_view name) const
{
    switch (CheckDump(dumpRoot, capture.folder, name))
    {
    case DumpState::Found:
        return dumpRoot / capture.folder / name;
    case DumpState::RootIsACapture:
        ADD_FAILURE() << MemoryDirVariable << " apunta a " << dumpRoot << ", que es una captura: tiene que apuntar "
                      << "a la carpeta que las contiene, memoria-cliente";
        return std::nullopt;
    case DumpState::Missing:
        break;
    }
    ADD_FAILURE() << "falta " << name << " en la captura " << capture.folder << " de " << dumpRoot;
    return std::nullopt;
}

} // namespace Client::Tests
