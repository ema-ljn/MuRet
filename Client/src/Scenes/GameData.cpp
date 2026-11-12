#include "Scenes/GameData.h"

#include "Core/Expected.h"
#include "Core/PathText.h"
#include "Data/DataFile.h"
#include "Data/DataPath.h"
#include "Data/Tables/GateTable.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Tables/SkillTable.h"
#include "Data/Text/Credits.h"
#include "Data/Text/FilterWords.h"
#include "Data/Text/SlideTable.h"
#include "Data/Text/TextTable.h"
#include "UI/MacroFile.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
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
// fscanf no tiene tope; MuRet lee hasta 1 MiB de Macro.txt, más que diez palabras de 0xff con cualquier blanco
// razonable entre ellas.
constexpr std::size_t MaxMacroFileBytes = 1024u * 1024u;

Core::Expected<Data::Tables::GateTable, std::string> LoadGates(const std::filesystem::path& dataRoot)
{
    std::error_code error;
    const auto path = Data::ResolveDataPath(dataRoot, GateTableFile, error);
    if (error)
    {
        return Core::Unexpected(
            std::format("no se pudo buscar {} en {}: {}", GateTableFile, Core::PathText(dataRoot), error.message()));
    }
    if (!path)
    {
        return Core::Unexpected(std::format("no está {} en {}", GateTableFile, Core::PathText(dataRoot)));
    }
    // 0x54a360 lee solo 256 registros de nueve bytes: el tamaño de la cola no cambia la carga.
    const auto file = Data::ReadDataFilePrefix(*path, Data::Tables::GateTableSize);
    if (!file.HasValue())
    {
        return Core::Unexpected(std::format("no se pudo leer {}: {}", GateTableFile, file.Error().message()));
    }
    auto table = Data::Tables::LoadGateTable(std::as_bytes(std::span(file.Value())));
    if (!table.HasValue())
    {
        return Core::Unexpected(std::format("{}: {}", GateTableFile, Data::DescribeDataError(table.Error())));
    }
    return std::move(table).Value();
}

// 0x54b9e0 lee un prefijo fijo y permite cualquier cola tras el checksum (Q-C01).
Core::Expected<Data::Tables::LoadedItemTable, std::string> LoadItems(const std::filesystem::path& dataRoot)
{
    std::error_code error;
    const auto path = Data::ResolveDataPath(dataRoot, ItemTableFile, error);
    if (error)
    {
        return Core::Unexpected(
            std::format("no se pudo buscar {} en {}: {}", ItemTableFile, Core::PathText(dataRoot), error.message()));
    }
    if (!path)
    {
        return Core::Unexpected(std::format("no está {} en {}", ItemTableFile, Core::PathText(dataRoot)));
    }
    const auto file = Data::ReadDataFilePrefix(*path, Data::Tables::ItemTableSize + sizeof(std::uint32_t));
    if (!file.HasValue())
    {
        return Core::Unexpected(std::format("no se pudo leer {}: {}", ItemTableFile, file.Error().message()));
    }
    auto table = Data::Tables::LoadItemTable(std::as_bytes(std::span(file.Value())));
    if (!table.HasValue())
    {
        return Core::Unexpected(std::format("{}: {}", ItemTableFile, Data::DescribeDataError(table.Error())));
    }
    return std::move(table).Value();
}

// Skill y conjuntos: 0x54acf0, 0x40c660 y 0x40c850 leen registros y checksum de un prefijo fijo.
template <typename Table, typename Loader>
Core::Expected<Table, std::string> LoadCheckedTable(const std::filesystem::path& dataRoot, std::string_view name,
                                                    std::size_t recordsSize, Loader load)
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
        return Core::Unexpected(std::format("no está {} en {}", name, Core::PathText(dataRoot)));
    }
    const auto file = Data::ReadDataFilePrefix(*path, recordsSize + sizeof(std::uint32_t));
    if (!file.HasValue())
    {
        return Core::Unexpected(std::format("no se pudo leer {}: {}", name, file.Error().message()));
    }
    auto table = load(std::as_bytes(std::span(file.Value())));
    if (!table.HasValue())
    {
        return Core::Unexpected(std::format("{}: {}", name, Data::DescribeDataError(table.Error())));
    }
    return std::move(table).Value();
}

// 0x40c600 desde 0x61ccb0 (0x61ecd5), después de Item.bmd (0x61eb37), con su argumento en 0: los tipos y
// después los conjuntos.
Core::Expected<Data::Tables::ItemSetTables, std::string> LoadItemSets(const std::filesystem::path& dataRoot)
{
    auto types = LoadCheckedTable<Data::Tables::ItemSetTypeTable>(
        dataRoot, ItemSetTypeFile, Data::Tables::ItemSetTypeTableSize, Data::Tables::LoadItemSetTypeTable);
    if (!types.HasValue())
    {
        return Core::Unexpected(std::move(types).Error());
    }
    auto options = LoadCheckedTable<Data::Tables::ItemSetOptionTable>(
        dataRoot, ItemSetOptionFile, Data::Tables::ItemSetOptionTableSize, Data::Tables::LoadItemSetOptionTable);
    if (!options.HasValue())
    {
        return Core::Unexpected(std::move(options).Error());
    }
    return Data::Tables::ItemSetTables{.types = std::move(types).Value(), .options = std::move(options).Value()};
}

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

// 0x63ff30: sin el archivo, el original avisa con un MessageBox y cierra el juego (WM_DESTROY); MuRet sigue con
// la tabla vacía, y los créditos se cierran al abrirlos (P-C78).
Core::Expected<std::vector<Data::Text::CreditEntry>, std::string> LoadCreditsFile(const std::filesystem::path& dataRoot)
{
    std::error_code error;
    const auto path = Data::ResolveDataPath(dataRoot, CreditsFile, error);
    if (error)
    {
        return Core::Unexpected(
            std::format("no se pudo buscar {} en {}: {}", CreditsFile, Core::PathText(dataRoot), error.message()));
    }
    if (!path)
    {
        return std::vector<Data::Text::CreditEntry>{};
    }
    const auto file = Data::ReadDataFile(*path, MaxFilterFileBytes);
    if (!file.HasValue())
    {
        return Core::Unexpected(std::format("no se pudo leer {}: {}", CreditsFile, file.Error().message()));
    }
    auto credits = Data::Text::LoadCredits(std::as_bytes(std::span(file.Value())));
    if (!credits.HasValue())
    {
        return Core::Unexpected(std::format("{}: {}", CreditsFile, Data::DescribeDataError(credits.Error())));
    }
    return std::move(credits).Value();
}

// 0x61c1c0: sin el archivo, o sin poder leerlo, las macros quedan vacías.
Core::Expected<UI::Macros, std::string> LoadMacros(const std::filesystem::path& dataRoot)
{
    std::error_code error;
    const auto path = Data::ResolveDataPath(dataRoot, MacroFile, error);
    if (error || !path)
        return UI::Macros{};
    const auto file = Data::ReadDataFilePrefix(*path, MaxMacroFileBytes);
    if (!file.HasValue())
        return UI::Macros{};
    auto macros = UI::ReadTheMacros(std::as_bytes(std::span(file.Value())));
    if (!macros.HasValue())
        return Core::Unexpected(std::format("{}: {}", MacroFile, macros.Error()));
    return std::move(macros).Value();
}

// 0x46b3b0 lee 0xa030 bytes sin mirar el tamaño: un archivo más largo se lee igual.
Core::Expected<std::optional<Data::Text::SlideTable>, std::string> LoadSlides(const std::filesystem::path& dataRoot)
{
    std::error_code error;
    const auto path = Data::ResolveDataPath(dataRoot, SlideTableFile, error);
    if (error)
    {
        return Core::Unexpected(
            std::format("no se pudo buscar {} en {}: {}", SlideTableFile, Core::PathText(dataRoot), error.message()));
    }
    if (!path)
        return std::optional<Data::Text::SlideTable>{};
    const auto file = Data::ReadDataFilePrefix(*path, Data::Text::SlideTableSize);
    if (!file.HasValue())
        return Core::Unexpected(std::format("no se pudo leer {}: {}", SlideTableFile, file.Error().message()));
    auto table = Data::Text::LoadSlideTable(std::as_bytes(std::span(file.Value())));
    if (!table.HasValue())
        return Core::Unexpected(std::format("{}: {}", SlideTableFile, Data::DescribeDataError(table.Error())));
    return std::optional<Data::Text::SlideTable>(std::move(table).Value());
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
    auto macros = LoadMacros(dataRoot);
    if (!macros.HasValue())
    {
        return Core::Unexpected(std::move(macros).Error());
    }
    data.macros = std::move(macros).Value();
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
    auto gates = LoadGates(dataRoot);
    if (!gates.HasValue())
    {
        return Core::Unexpected(std::move(gates).Error());
    }
    data.gates.emplace(std::move(gates).Value());
    auto items = LoadItems(dataRoot);
    if (!items.HasValue())
    {
        return Core::Unexpected(std::move(items).Error());
    }
    data.items.emplace(std::move(items).Value());
    auto itemSets = LoadItemSets(dataRoot);
    if (!itemSets.HasValue())
    {
        return Core::Unexpected(std::move(itemSets).Error());
    }
    data.itemSets.emplace(std::move(itemSets).Value());
    auto skills = LoadCheckedTable<Data::Tables::SkillTable>(dataRoot, SkillTableFile, Data::Tables::SkillTableSize,
                                                             Data::Tables::LoadSkillTable);
    if (!skills.HasValue())
    {
        return Core::Unexpected(std::move(skills).Error());
    }
    data.skills.emplace(std::move(skills).Value());
    auto credits = LoadCreditsFile(dataRoot);
    if (!credits.HasValue())
    {
        return Core::Unexpected(std::move(credits).Error());
    }
    data.credits = std::move(credits).Value();
    auto slides = LoadSlides(dataRoot);
    if (!slides.HasValue())
    {
        return Core::Unexpected(std::move(slides).Error());
    }
    data.slides = std::move(slides).Value();
    return data;
}
} // namespace Client::Scenes
