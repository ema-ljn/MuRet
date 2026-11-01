#include "support/GoldenDir.h"

#include "Data/DataPath.h"

#include <cstdlib>
#include <string>
#include <system_error>

namespace
{

constexpr const char* GoldenDirVariable = "MURET_CLIENT_GOLDEN_DIR";

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

void GoldenDataTest::SetUp()
{
    const std::optional<std::filesystem::path> dir = GoldenDir();
    const GoldenDirState state = CheckGoldenDir(dir);
    if (state == GoldenDirState::NotSet)
    {
        GTEST_SKIP() << GoldenDirVariable << " no está definida: test salteado";
    }
    if (state == GoldenDirState::Missing)
    {
        FAIL() << GoldenDirVariable << " apunta a " << *dir << ", que no es una carpeta";
    }
    dataRoot = *dir;
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

} // namespace Client::Tests
