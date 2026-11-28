#include "Tables/GameData.h"

#include "Core/Expected.h"
#include "Core/PathText.h"
#include "Data/DataFile.h"
#include "Data/DataPath.h"
#include "Tables/GateTable.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Tables/MonsterSkillTable.h"
#include "Tables/QuestTable.h"
#include "Tables/SkillTable.h"
#include "Tables/Credits.h"
#include "Tables/DialogTable.h"
#include "Tables/FilterWords.h"
#include "Tables/MoveRequirements.h"
#include "Tables/NpcNames.h"
#include "Tables/SlideTable.h"
#include "Tables/TextTable.h"
#include "Chat/MacroFile.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Client::Tables
{
namespace
{
// More than the whole table (0x96000 bytes): a larger file is not a Text.bmd.
constexpr std::uintmax_t MaxTextTableFileBytes = 1024u * 1024u;
// And more than a filter's 20,004 bytes.
constexpr std::uintmax_t MaxFilterFileBytes = 64u * 1024u;
// fscanf has no cap; MuRet reads up to 1 MiB of Macro.txt, more than ten 0xff words with any reasonable whitespace
// between them.
constexpr std::size_t MaxMacroFileBytes = 1024u * 1024u;

Core::Expected<GateTable, std::string> LoadGates(const std::filesystem::path& dataRoot)
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
    // 0x54a360 reads only 256 nine-byte records: the tail's size does not change the load.
    const auto file = Data::ReadDataFilePrefix(*path, GateTableSize);
    if (!file.HasValue())
    {
        return Core::Unexpected(std::format("no se pudo leer {}: {}", GateTableFile, file.Error().message()));
    }
    auto table = LoadGateTable(std::as_bytes(std::span(file.Value())));
    if (!table.HasValue())
    {
        return Core::Unexpected(std::format("{}: {}", GateTableFile, Data::DescribeDataError(table.Error())));
    }
    return std::move(table).Value();
}

// 0x54b9e0 reads a fixed prefix and allows any tail after the checksum (Q-C01).
Core::Expected<LoadedItemTable, std::string> LoadItems(const std::filesystem::path& dataRoot)
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
    const auto file = Data::ReadDataFilePrefix(*path, ItemTableSize + sizeof(std::uint32_t));
    if (!file.HasValue())
    {
        return Core::Unexpected(std::format("no se pudo leer {}: {}", ItemTableFile, file.Error().message()));
    }
    auto table = LoadItemTable(std::as_bytes(std::span(file.Value())));
    if (!table.HasValue())
    {
        return Core::Unexpected(std::format("{}: {}", ItemTableFile, Data::DescribeDataError(table.Error())));
    }
    return std::move(table).Value();
}

// 0x54acf0, 0x40c660 and 0x40c850 read records and checksum from a fixed prefix.
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

// 0x40c600 from 0x61ccb0 (0x61ecd5), after Item.bmd (0x61eb37), with its argument at 0: the types, then the sets.
Core::Expected<ItemSetTables, std::string> LoadItemSets(const std::filesystem::path& dataRoot)
{
    auto types =
        LoadCheckedTable<ItemSetTypeTable>(dataRoot, ItemSetTypeFile, ItemSetTypeTableSize, LoadItemSetTypeTable);
    if (!types.HasValue())
    {
        return Core::Unexpected(std::move(types).Error());
    }
    auto options = LoadCheckedTable<ItemSetOptionTable>(dataRoot, ItemSetOptionFile, ItemSetOptionTableSize,
                                                        LoadItemSetOptionTable);
    if (!options.HasValue())
    {
        return Core::Unexpected(std::move(options).Error());
    }
    return ItemSetTables{.types = std::move(types).Value(), .options = std::move(options).Value()};
}

// Or none if it is missing.
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
    auto words = LoadFilterWords(std::as_bytes(std::span(file.Value())), checksumKey);
    if (!words.HasValue())
    {
        return Core::Unexpected(std::format("{}: {}", name, Data::DescribeDataError(words.Error())));
    }
    return std::move(words).Value();
}

// 0x63ff30: without the file, the original shows a MessageBox and closes the game (WM_DESTROY); MuRet goes on with an
// empty table, and the credits close when opened (P-C78).
Core::Expected<std::vector<CreditEntry>, std::string> LoadCreditsFile(const std::filesystem::path& dataRoot)
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
        return std::vector<CreditEntry>{};
    }
    const auto file = Data::ReadDataFile(*path, MaxFilterFileBytes);
    if (!file.HasValue())
    {
        return Core::Unexpected(std::format("no se pudo leer {}: {}", CreditsFile, file.Error().message()));
    }
    auto credits = LoadCredits(std::as_bytes(std::span(file.Value())));
    if (!credits.HasValue())
    {
        return Core::Unexpected(std::format("{}: {}", CreditsFile, Data::DescribeDataError(credits.Error())));
    }
    return std::move(credits).Value();
}

// 0x61c1c0: without the file, or unable to read it, the macros stay empty.
Core::Expected<Tables::Macros, std::string> LoadMacros(const std::filesystem::path& dataRoot)
{
    std::error_code error;
    const auto path = Data::ResolveDataPath(dataRoot, MacroFile, error);
    if (error || !path)
        return Tables::Macros{};
    const auto file = Data::ReadDataFilePrefix(*path, MaxMacroFileBytes);
    if (!file.HasValue())
        return Tables::Macros{};
    auto macros = Tables::ReadTheMacros(std::as_bytes(std::span(file.Value())));
    if (!macros.HasValue())
        return Core::Unexpected(std::format("{}: {}", MacroFile, macros.Error()));
    return std::move(macros).Value();
}

// 0x46b3b0 reads 0xa030 bytes without checking the size: a longer file reads the same.
Core::Expected<std::optional<SlideTable>, std::string> LoadSlides(const std::filesystem::path& dataRoot)
{
    std::error_code error;
    const auto path = Data::ResolveDataPath(dataRoot, SlideTableFile, error);
    if (error)
    {
        return Core::Unexpected(
            std::format("no se pudo buscar {} en {}: {}", SlideTableFile, Core::PathText(dataRoot), error.message()));
    }
    if (!path)
        return std::optional<SlideTable>{};
    const auto file = Data::ReadDataFilePrefix(*path, SlideTableSize);
    if (!file.HasValue())
        return Core::Unexpected(std::format("no se pudo leer {}: {}", SlideTableFile, file.Error().message()));
    auto table = LoadSlideTable(std::as_bytes(std::span(file.Value())));
    if (!table.HasValue())
        return Core::Unexpected(std::format("{}: {}", SlideTableFile, Data::DescribeDataError(table.Error())));
    return std::optional<SlideTable>(std::move(table).Value());
}
// 0x54b2b0 and 0x418d10: 200 fixed-size records. Without the file, `empty`; otherwise the loader with the prefix.
template <typename Table, typename Loader>
Core::Expected<Table, std::string> LoadFixedTable(const std::filesystem::path& dataRoot, std::string_view name,
                                                  std::size_t size, Table empty, Loader load)
{
    std::error_code error;
    const auto path = Data::ResolveDataPath(dataRoot, name, error);
    if (error)
    {
        return Core::Unexpected(
            std::format("no se pudo buscar {} en {}: {}", name, Core::PathText(dataRoot), error.message()));
    }
    if (!path)
        return empty;
    const auto file = Data::ReadDataFilePrefix(*path, size);
    if (!file.HasValue())
        return Core::Unexpected(std::format("no se pudo leer {}: {}", name, file.Error().message()));
    auto table = load(std::as_bytes(std::span(file.Value())));
    if (!table.HasValue())
        return Core::Unexpected(std::format("{}: {}", name, Data::DescribeDataError(table.Error())));
    return std::move(table).Value();
}

// 0x448440 reads the count and count · 0x50 bytes with no cap; MuRet reads up to 1 MiB (13,107 destinations).
constexpr std::size_t MaxMoveRequirementsBytes = 1024u * 1024u;

// 0x448440: without the file, the empty list and nothing else.
Core::Expected<std::vector<MoveRequirement>, std::string> LoadMoves(const std::filesystem::path& dataRoot)
{
    std::error_code error;
    const auto path = Data::ResolveDataPath(dataRoot, MoveRequirementsFile, error);
    if (error)
    {
        return Core::Unexpected(std::format("no se pudo buscar {} en {}: {}", MoveRequirementsFile,
                                            Core::PathText(dataRoot), error.message()));
    }
    if (!path)
        return std::vector<MoveRequirement>{};
    const auto file = Data::ReadDataFilePrefix(*path, MaxMoveRequirementsBytes);
    if (!file.HasValue())
        return Core::Unexpected(std::format("no se pudo leer {}: {}", MoveRequirementsFile, file.Error().message()));
    auto moves = LoadMoveRequirements(std::as_bytes(std::span(file.Value())));
    if (!moves.HasValue())
        return Core::Unexpected(std::format("{}: {}", MoveRequirementsFile, Data::DescribeDataError(moves.Error())));
    return std::move(moves).Value();
}
// 0x54a440 reads the count and count · 0x1c bytes with no cap; MuRet reads up to 1 MiB.
constexpr std::size_t MaxMonsterSkillBytes = 1024u * 1024u;

// 0x54a440: without the file, the .bss table.
Core::Expected<MonsterSkillTable, std::string> LoadMonsterSkills(const std::filesystem::path& dataRoot)
{
    std::error_code error;
    const auto path = Data::ResolveDataPath(dataRoot, MonsterSkillTableFile, error);
    if (error)
    {
        return Core::Unexpected(std::format("no se pudo buscar {} en {}: {}", MonsterSkillTableFile,
                                            Core::PathText(dataRoot), error.message()));
    }
    if (!path)
        return MonsterSkillTable{};
    const auto file = Data::ReadDataFilePrefix(*path, MaxMonsterSkillBytes);
    if (!file.HasValue())
        return Core::Unexpected(std::format("no se pudo leer {}: {}", MonsterSkillTableFile, file.Error().message()));
    auto table = LoadMonsterSkillTable(std::as_bytes(std::span(file.Value())));
    if (!table.HasValue())
        return Core::Unexpected(std::format("{}: {}", MonsterSkillTableFile, Data::DescribeDataError(table.Error())));
    return std::move(table).Value();
}

// 0x54e7f0 reads with getc to the end with no cap; MuRet reads up to 1 MiB (the lab's has 10 KB).
constexpr std::uintmax_t MaxNpcNameFileBytes = 1024u * 1024u;

// 0x54e7f0: without the file (fopen gives NULL), the table stays as it was.
Core::Expected<std::vector<NpcNameRecord>, std::string> LoadNpcNameFile(const std::filesystem::path& dataRoot)
{
    std::error_code error;
    const auto path = Data::ResolveDataPath(dataRoot, NpcNameFile, error);
    if (error)
    {
        return Core::Unexpected(
            std::format("no se pudo buscar {} en {}: {}", NpcNameFile, Core::PathText(dataRoot), error.message()));
    }
    if (!path)
        return std::vector<NpcNameRecord>{};
    const auto file = Data::ReadDataFile(*path, MaxNpcNameFileBytes);
    if (!file.HasValue())
        return Core::Unexpected(std::format("no se pudo leer {}: {}", NpcNameFile, file.Error().message()));
    auto names = LoadNpcNames(std::as_bytes(std::span(file.Value())));
    if (!names.HasValue())
        return Core::Unexpected(std::format("{}: {}", NpcNameFile, Data::DescribeDataError(names.Error())));
    return std::move(names).Value();
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
    auto table = LoadTextTable(std::as_bytes(std::span(file.Value())));
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
    auto filter = LoadFilter(dataRoot, FilterFile, FilterChecksumKey);
    if (!filter.HasValue())
    {
        return Core::Unexpected(std::move(filter).Error());
    }
    data.filter = std::move(filter).Value();
    auto filterName = LoadFilter(dataRoot, FilterNameFile, FilterNameChecksumKey);
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
    auto dialogs =
        LoadFixedTable(dataRoot, DialogTableFile, DialogTableSize, DialogTable(DialogCount), LoadDialogTable);
    if (!dialogs.HasValue())
    {
        return Core::Unexpected(std::move(dialogs).Error());
    }
    data.dialogs = std::move(dialogs).Value();
    auto quests = LoadFixedTable(dataRoot, QuestTableFile, QuestTableSize, QuestTable(QuestCount), LoadQuestTable);
    if (!quests.HasValue())
    {
        return Core::Unexpected(std::move(quests).Error());
    }
    data.quests = std::move(quests).Value();
    auto moves = LoadMoves(dataRoot);
    if (!moves.HasValue())
    {
        return Core::Unexpected(std::move(moves).Error());
    }
    data.moves = std::move(moves).Value();
    auto itemSets = LoadItemSets(dataRoot);
    if (!itemSets.HasValue())
    {
        return Core::Unexpected(std::move(itemSets).Error());
    }
    data.itemSets.emplace(std::move(itemSets).Value());
    // 0x61ecdf: MonsterSkill.bmd, after movereq.bmd (0x61ec8f) and the set tables (0x61ecd5).
    auto monsterSkills = LoadMonsterSkills(dataRoot);
    if (!monsterSkills.HasValue())
    {
        return Core::Unexpected(std::move(monsterSkills).Error());
    }
    data.monsterSkills = std::move(monsterSkills).Value();
    auto npcNames = LoadNpcNameFile(dataRoot);
    if (!npcNames.HasValue())
    {
        return Core::Unexpected(std::move(npcNames).Error());
    }
    data.npcNames = std::move(npcNames).Value();
    auto skills = LoadCheckedTable<SkillTable>(dataRoot, SkillTableFile, SkillTableSize, LoadSkillTable);
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
Core::Expected<void, std::string> WriteMacroFile(const std::filesystem::path& dataRoot, std::string_view content)
{
    std::error_code error;
    std::optional<std::filesystem::path> path = Data::ResolveDataPath(dataRoot, MacroFile, error);
    if (error)
        return Core::Unexpected(
            std::format("no se pudo buscar {} en {}: {}", MacroFile, Core::PathText(dataRoot), error.message()));
    if (!path)
        path = dataRoot / MacroFile;
    std::ofstream file(*path, std::ios::binary | std::ios::trunc);
    if (!file)
        return Core::Unexpected(std::format("0x61c17c: no se pudo abrir {} para escribir (el original sigue con "
                                            "fprintf sobre NULL)",
                                            Core::PathText(*path)));
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!file)
        return Core::Unexpected(std::format("no se pudo escribir {}", Core::PathText(*path)));
    return {};
}
} // namespace Client::Tables
