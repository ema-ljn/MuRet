#include "support/GoldenDir.h"

#include "Data/DataPath.h"

#include <cstdlib>
#include <string>

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

std::optional<std::filesystem::path> GoldenDir()
{
    const std::optional<std::string> value = ReadEnvironment(GoldenDirVariable);
    if (!value || value->empty())
    {
        return std::nullopt;
    }
    return std::filesystem::path(*value);
}

void GoldenDataTest::SetUp()
{
    const std::optional<std::filesystem::path> dir = GoldenDir();
    if (!dir)
    {
        GTEST_SKIP() << GoldenDirVariable << " no está definida: test salteado";
    }
    ASSERT_TRUE(std::filesystem::is_directory(*dir))
        << GoldenDirVariable << " apunta a " << *dir << ", que no es una carpeta";
    dataRoot = *dir;
}

std::optional<std::filesystem::path> GoldenDataTest::DataFile(std::string_view originalPath) const
{
    std::optional<std::filesystem::path> path = Data::ResolveDataPath(dataRoot, originalPath);
    if (!path)
    {
        ADD_FAILURE() << "falta " << originalPath << " en la carpeta golden " << dataRoot;
    }
    return path;
}

} // namespace Client::Tests
