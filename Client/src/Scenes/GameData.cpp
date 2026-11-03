#include "Scenes/GameData.h"

#include "Core/Expected.h"
#include "Core/PathText.h"
#include "Data/DataFile.h"
#include "Data/DataPath.h"
#include "Data/Text/TextTable.h"

#include <cstdint>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <system_error>
#include <utility>

namespace Client::Scenes
{
namespace
{
// Más que la tabla entera (0x96000 bytes): un archivo más grande no es un Text.bmd.
constexpr std::uintmax_t MaxTextTableFileBytes = 1024u * 1024u;
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
    return data;
}
} // namespace Client::Scenes
