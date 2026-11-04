#include "Scenes/GameData.h"

#include "Core/Expected.h"
#include "Core/PathText.h"
#include "Data/DataFile.h"
#include "Data/DataPath.h"
#include "Data/Text/FilterWords.h"
#include "Data/Text/TextTable.h"

#include <cstdint>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Client::Scenes
{
namespace
{
// Más que la tabla entera (0x96000 bytes): un archivo más grande no es un Text.bmd.
constexpr std::uintmax_t MaxTextTableFileBytes = 1024u * 1024u;
// Y más que los 20.004 bytes de un filtro.
constexpr std::uintmax_t MaxFilterFileBytes = 64u * 1024u;

// Las palabras de `name` en `dataRoot`, o ninguna si no está.
Core::Expected<std::vector<std::string>, std::string> LoadFilter(const std::filesystem::path& dataRoot,
                                                                 std::string_view name, std::uint32_t checksumKey)
{
    std::error_code error;
    const auto path = Data::ResolveDataPath(dataRoot, name, error);
    if (error)
    {
        return Core::Unexpected(
            std::format("no se pudo buscar {} en {}: {}", name, Core::PathText(dataRoot), error.message()));
    }
    if (!path)
    {
        return std::vector<std::string>{};
    }
    const auto file = Data::ReadDataFile(*path, MaxFilterFileBytes);
    if (!file.HasValue())
    {
        return Core::Unexpected(std::format("no se pudo leer {}: {}", name, file.Error().message()));
    }
    auto words = Data::Text::LoadFilterWords(std::as_bytes(std::span(file.Value())), checksumKey);
    if (!words.HasValue())
    {
        return Core::Unexpected(std::format("{}: {}", name, Data::DescribeDataError(words.Error())));
    }
    return std::move(words).Value();
}
} // namespace

Core::Expected<GameData, std::string> LoadGameData(const std::filesystem::path& dataRoot)
{
    std::error_code error;
    const auto path = Data::ResolveDataPath(dataRoot, TextTableFile, error);
    if (error)
    {
        return Core::Unexpected(
            std::format("no se pudo buscar {} en {}: {}", TextTableFile, Core::PathText(dataRoot), error.message()));
    }
    if (!path)
    {
        return Core::Unexpected(std::format("no está {} en {}", TextTableFile, Core::PathText(dataRoot)));
    }
    const auto file = Data::ReadDataFile(*path, MaxTextTableFileBytes);
    if (!file.HasValue())
    {
        return Core::Unexpected(std::format("no se pudo leer {}: {}", TextTableFile, file.Error().message()));
    }
    auto table = Data::Text::LoadTextTable(std::as_bytes(std::span(file.Value())));
    if (!table.HasValue())
    {
        return Core::Unexpected(std::format("{}: {}", TextTableFile, Data::DescribeDataError(table.Error())));
    }
    GameData data;
    data.text.emplace(std::move(table).Value());
    auto filter = LoadFilter(dataRoot, FilterFile, Data::Text::FilterChecksumKey);
    if (!filter.HasValue())
    {
        return Core::Unexpected(std::move(filter).Error());
    }
    data.filter = std::move(filter).Value();
    auto filterName = LoadFilter(dataRoot, FilterNameFile, Data::Text::FilterNameChecksumKey);
    if (!filterName.HasValue())
    {
        return Core::Unexpected(std::move(filterName).Error());
    }
    data.filterName = std::move(filterName).Value();
    return data;
}
} // namespace Client::Scenes
