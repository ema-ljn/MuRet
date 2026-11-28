#include "Core/Expected.h"
#include "Hud/GameHud.h"
#include "Items/HudItems.h"
#include "Items/ItemNames.h"
#include "Tooltips/ItemTooltipParts.h"
#include "Monsters/NpcNames.h"
#include "Tooltips/TooltipLines.h"
#include "Tooltips/TooltipWriter.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Kernel/ItemState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

// 0x5b371b..0x5b45f4, the item body's name (06 § Los tooltips, "El cuerpo del ítem"). The texts are the Text.bmd ones
// (the address − 0x76bd1dc, over 300).
namespace Client::Tooltips
{
namespace
{
using Frame = ItemTooltipFrame;

// A case of the per-level name: the Text.bmd text as a format, the record's name with "%s" or nothing (the line stays
// empty and the box ends there).
enum class NameKind : std::uint8_t
{
    None,
    RecordName,
    RecordAsFormat,
    Text,
};

struct NameCase
{
    NameKind kind = NameKind::None;
    std::size_t text = 0;
};

constexpr NameCase NoName{};
constexpr NameCase Named{.kind = NameKind::RecordName};
// 0x1a14 with level 0 (0x5b3b91): sprintf with the record's name as the format.
constexpr NameCase NameAsFormat{.kind = NameKind::RecordAsFormat};
constexpr NameCase TextName(std::size_t text)
{
    return NameCase{.kind = NameKind::Text, .text = text};
}

// The types with a per-level name (0x5b3737..0x5b3cf9), from level 0 on; beyond, NoName.
struct LeveledName
{
    std::uint16_t type = 0;
    bool gold = false;
    std::array<NameCase, 4> levels{};
};

constexpr std::array<LeveledName, 11> LeveledNames{{
    {.type = 0x1c17, .gold = true, .levels = {Named, TextName(906), NoName, NoName}},
    {.type = 0x1c18, .gold = true, .levels = {Named, TextName(907), NoName, NoName}},
    {.type = 0x1c0c, .gold = false, .levels = {TextName(100), TextName(101), NoName, NoName}},
    {.type = 0x1a0e, .gold = true, .levels = {Named, TextName(1235), NoName, NoName}},
    {.type = 0x1c15, .gold = true, .levels = {Named, TextName(810), NoName, TextName(1290)}},
    {.type = 0x1a13, .gold = true, .levels = {TextName(811), TextName(812), TextName(817), NoName}},
    {.type = 0x1a14, .gold = true, .levels = {NameAsFormat, TextName(922), TextName(928), TextName(929)}},
    {.type = 0x1c07, .gold = false, .levels = {TextName(1413), TextName(1414), NoName, NoName}},
    {.type = 0x1a07, .gold = false, .levels = {TextName(1460), TextName(1461), NoName, NoName}},
    {.type = 0x1a0b, .gold = false, .levels = {TextName(1416), TextName(1462), NoName, NoName}},
    {.type = 0x1c09, .gold = false, .levels = {Named, TextName(108), NoName, NoName}},
}};

// 0x1c0b, the table at 0x5b80a8: from level 8 to 12, "%s +%d" with 115 and the level − 7.
constexpr std::uint16_t LuckyBox = 0x1c0b;
constexpr std::array<NameCase, 16> LuckyBoxNames{
    Named,  TextName(105), TextName(106), TextName(107), NoName, TextName(109), TextName(110),  TextName(111),
    NoName, NoName,        NoName,        NoName,        NoName, TextName(117), TextName(1650), TextName(1651)};
constexpr std::int32_t FirstNumberedLuckyBox = 8;
constexpr std::int32_t LastNumberedLuckyBox = 12;
constexpr std::int32_t LuckyBoxNumberBase = 7;
constexpr std::size_t LuckyBoxText = 115;
// 0x1c0c with level 2: the text from the .data table at 0x6c9350 (100 bytes per durability).
constexpr std::uint16_t EventTicket = 0x1c0c;
constexpr std::int32_t EventTicketTableLevel = 2;
// 0x1c19 and 0x1c1a: "%s" in gold.
constexpr std::uint16_t FirstGoldQuest = 0x1c19;
constexpr std::uint16_t LastGoldQuest = 0x1c1a;
// 0x1a0f: "%s %s" with the level's stat (0 to 4) and the name.
constexpr std::uint16_t StatFruit = 0x1a0f;
constexpr std::array<std::size_t, 5> StatTexts{168, 169, 167, 166, 1900};
// 0x180b: "%s %s" with the name of Skill.bmd's entry 30 + level and 102.
constexpr std::uint16_t SkillJewel = 0x180b;
constexpr std::uint32_t FirstJewelSkill = 30;
constexpr std::size_t JewelText = 102;
// 0x1a0a searches the table at 0x7681eb8 (in steps of 0x36 bytes up to 0x7688ab8) for the word of 0x6c9338[level].
constexpr std::uint16_t TransformationRing = 0x1a0a;
constexpr std::size_t TransformationRingText = 103;
// 0x1a1f: "%s %s" with 1187 or 1214 in front.
constexpr std::uint16_t PetSpirit = 0x1a1f;
constexpr std::array<std::size_t, 2> PetSpiritTexts{1187, 1214};
// 0x1c1d: 1180 with the level; 0x1c1c, "%s +%d" in gold.
constexpr std::uint16_t MarkOfKundun = 0x1c1d;
constexpr std::size_t MarkOfKundunText = 1180;
constexpr std::uint16_t LostMap = 0x1c1c;
// 0x1c20 to 0x1c22: "%s".
constexpr std::uint16_t FirstSiegePotion = 0x1c20;
constexpr std::uint16_t LastSiegePotion = 0x1c22;
// 0x1a20 to 0x1a24 in white and 0x1a25 in blue, with 1863 or 1864 after it depending on D+0x1b & 0x3f.
constexpr std::uint16_t FirstBox = 0x1a20;
constexpr std::uint16_t LastWhiteBox = 0x1a24;
constexpr std::uint16_t BlueBox = 0x1a25;
constexpr std::array<std::size_t, 2> BlueBoxTexts{1863, 1864};
// The ones with "%s" at level 0 and "%s +%d" at the others (0x5b4563): the wings from 0x1803 to 0x1806, the cape
// 0x1a1e and the four of 0x5b43e1.
constexpr std::uint16_t FirstHighWing = 0x1803;
constexpr std::uint16_t LastHighWing = 0x1806;
constexpr std::uint16_t Cape = 0x1a1e;
constexpr std::array<std::uint16_t, 4> PlusNamed{0x13, 0x812, 0xa0a, 0x40d};
// The Bless and Soul bundles: "%s +%d" with 1806 or 1807 and the level + 1.
constexpr std::uint16_t BlessBundle = 0x181e;
constexpr std::uint16_t SoulBundle = 0x181f;
constexpr std::size_t BlessText = 1806;
constexpr std::size_t SoulText = 1807;
// The generic one: 620 in front with some excellent option; [EBP−0x6c] is 64 bytes.
constexpr std::size_t ExcellentText = 620;
constexpr std::size_t NameBufferBytes = 0x40;
constexpr std::uint8_t ExcellentBits = 0x3f;

bool InRange(std::int32_t type, std::uint16_t first, std::uint16_t last)
{
    return type >= first && type <= last;
}

void WriteTheCase(TooltipWriter& lines, const NameCase& name, std::string_view record)
{
    switch (name.kind)
    {
    case NameKind::RecordName:
        lines.Print("%s", record);
        return;
    case NameKind::RecordAsFormat:
        lines.Print(record);
        return;
    case NameKind::Text:
        lines.PrintText(name.text);
        return;
    case NameKind::None:
        return;
    }
}

// "%s" at level 0 and "%s +%d" at the others.
void WritePlusName(TooltipWriter& lines, std::string_view name, std::int32_t level)
{
    if (level == 0)
        lines.Print("%s", name);
    else
        lines.Print("%s +%d", name, level);
}

// 0x5b3737..0x5b3cf9 and 0x5b3881..0x5b39a7: the per-level names. false if `type` is not one of them.
Core::Expected<bool, std::string> WriteALeveledName(Frame& frame, std::int32_t type, std::string_view record)
{
    TooltipWriter& lines = frame.lines;
    const std::int32_t level = frame.level;
    if (type == LuckyBox)
    {
        if (level >= FirstNumberedLuckyBox && level <= LastNumberedLuckyBox)
            lines.Print("%s +%d", lines.Text(LuckyBoxText), level - LuckyBoxNumberBase);
        else
            WriteTheCase(lines, LuckyBoxNames[static_cast<std::size_t>(level)], record);
        return true;
    }
    // 0x5b3824..0x5b3852: sprintf(line, "%s" (0x6c99e4), 0x6c9350 + D+0x1a · 100): the prize by the item's durability,
    // not by Item.bmd+0x2d like 0x5b1c70. It continues at 0x5b4598, like the others.
    if (type == EventTicket && level == EventTicketTableLevel)
    {
        const std::uint32_t index = frame.item.durability;
        const std::optional<std::string_view> prize = Items::EventTicketPrizeOf(index);
        if (!prize)
            return Core::Unexpected(std::format("0x5b3835: el premio {} del boleto (su durabilidad) lee 0x6c9350 + {} "
                                                "· 100, en la .data que sigue a los diez premios, que MuRet no tiene",
                                                index, index));
        lines.Print("%s", *prize);
        return true;
    }
    for (const LeveledName& leveled : LeveledNames)
    {
        if (leveled.type != type)
            continue;
        if (leveled.gold)
            frame.nameColor = TooltipGold;
        if (static_cast<std::size_t>(level) < leveled.levels.size())
            WriteTheCase(lines, leveled.levels[static_cast<std::size_t>(level)], record);
        return true;
    }
    return false;
}

// 0x5b3d09..0x5b41cb: the name of Skill.bmd's entry 30 + level with the access open, and 102.
Core::Expected<void, std::string> WriteTheJewelName(Frame& frame)
{
    const auto entry = ReadTheSkillEntry(frame, FirstJewelSkill + static_cast<std::uint32_t>(frame.level), "0x5b3d09");
    if (!entry.HasValue())
        return Core::Unexpected(entry.Error());
    frame.lines.Print("%s %s", std::string_view(entry.Value().name), frame.lines.Text(JewelText));
    return {};
}

// 0x5b4251..0x5b445e: the pets, the seals, the siege potions, the boxes, the "+%d" ones and the bundles. false if
// `type` is not one of them.
bool WriteASpecialName(Frame& frame, std::int32_t type, std::string_view record)
{
    TooltipWriter& lines = frame.lines;
    const std::int32_t level = frame.level;
    if (type == PetSpirit)
    {
        if (level == 0 || level == 1)
            lines.Print("%s %s", lines.Text(PetSpiritTexts[static_cast<std::size_t>(level)]), record);
        return true;
    }
    if (type == MarkOfKundun)
    {
        lines.PrintText(MarkOfKundunText, level);
        return true;
    }
    if (type == LostMap)
    {
        lines.Print("%s +%d", record, level);
        frame.nameColor = TooltipGold;
        return true;
    }
    if (InRange(type, FirstSiegePotion, LastSiegePotion))
    {
        lines.Print("%s", record);
        return true;
    }
    if (InRange(type, FirstBox, LastWhiteBox))
    {
        frame.nameColor = TooltipWhite;
        lines.Print("%s", record);
        return true;
    }
    if (type == BlueBox)
    {
        frame.nameColor = TooltipBlue;
        const auto excellent = static_cast<std::uint8_t>(frame.item.flags1b & ExcellentBits);
        if (excellent == 1 || excellent == 2)
            lines.Print("%s %s", record, lines.Text(BlueBoxTexts[excellent - 1u]));
        else
            lines.Print("%s", record);
        return true;
    }
    const bool plus = InRange(type, FirstHighWing, LastHighWing) || type == Cape ||
                      std::ranges::find(PlusNamed, type) != PlusNamed.end();
    if (plus)
    {
        WritePlusName(lines, record, level);
        return true;
    }
    if (type == BlessBundle || type == SoulBundle)
    {
        lines.Print("%s +%d", lines.Text(type == BlessBundle ? BlessText : SoulText), level + 1);
        return true;
    }
    return false;
}

// 0x5b445e..0x5b4590: the generic one, with the set's name (0x40cf30) in front and 620 with some excellent option.
Core::Expected<void, std::string> WriteTheGenericName(Frame& frame, std::string_view record)
{
    const auto set = ItemSetNameOf(frame.context, frame.item);
    if (!set.HasValue())
        return Core::Unexpected(set.Error());
    const std::string name = set.Value().value_or(std::string{}) + std::string(record);
    if (name.size() + 1 > NameBufferBytes)
        return Core::Unexpected(std::format("0x5b4499: el nombre \"{}\" pasa de los 64 bytes de [EBP-0x6c] y pisa la "
                                            "pila",
                                            name));
    TooltipWriter& lines = frame.lines;
    if (IsExcellent(frame.item))
    {
        if (frame.level == 0)
            lines.Print("%s %s", lines.Text(ExcellentText), std::string_view(name));
        else
            lines.Print("%s %s +%d", lines.Text(ExcellentText), std::string_view(name), frame.level);
        return {};
    }
    WritePlusName(lines, name, frame.level);
    return {};
}

// 0x5b41d0..0x5b421a: the first record of 0x7681eb8 whose type (the word, unsigned) is the level's monster (0x6c9338),
// and sprintf(line, "%s %s" (0x6c9a38), the record's +2, 103) in the tail of 0x5b4582. With none, it jumps to 0x5b459d
// without writing the line.
Core::Expected<void, std::string> WriteTheTransformationRingName(Frame& frame)
{
    const std::optional<std::uint32_t> monster = Items::TransformationMonsterOf(frame.level);
    if (!monster)
        return Core::Unexpected(std::format("0x5b41dc: el nivel {} del anillo de transformación lee 0x6c9338 fuera de "
                                            "lo que MuRet tiene de la .data",
                                            frame.level));
    const std::optional<std::string> name =
        Monsters::NpcNameOf(frame.context.world.Runtime().npcNames, static_cast<std::int32_t>(*monster));
    if (!name)
        return {};
    TooltipWriter& lines = frame.lines;
    lines.Print("%s %s", std::string_view(*name), lines.Text(TransformationRingText));
    return {};
}

Core::Expected<void, std::string> WriteTheNameLine(Frame& frame, std::string_view record)
{
    TooltipWriter& lines = frame.lines;
    const std::int32_t type = static_cast<std::int16_t>(frame.item.id);
    if (InRange(type, FirstGoldQuest, LastGoldQuest))
    {
        lines.Print("%s", record);
        frame.nameColor = TooltipGold;
        return {};
    }
    const auto leveled = WriteALeveledName(frame, type, record);
    if (!leveled.HasValue())
        return Core::Unexpected(leveled.Error());
    if (leveled.Value())
        return {};
    if (type == StatFruit)
    {
        frame.nameColor = TooltipGold;
        if (static_cast<std::size_t>(frame.level) < StatTexts.size())
            lines.Print("%s %s", lines.Text(StatTexts[static_cast<std::size_t>(frame.level)]), record);
        return {};
    }
    if (type == SkillJewel)
        return WriteTheJewelName(frame);
    if (type == TransformationRing)
        return WriteTheTransformationRingName(frame);
    if (WriteASpecialName(frame, type, record))
        return {};
    return WriteTheGenericName(frame, record);
}
} // namespace

Core::Expected<void, std::string> WriteTheItemName(ItemTooltipFrame& frame)
{
    frame.lines.At("0x5b371b");
    const std::string record = Items::ItemRecordName(frame.record);
    if (auto written = WriteTheNameLine(frame, record); !written.HasValue())
        return written;
    // 0x5b459d..0x5b45ef.
    TooltipWriter& lines = frame.lines;
    lines.At("0x5b459d");
    lines.Color(frame.nameColor);
    lines.Bold(1);
    lines.Next();
    lines.NewLine();
    return {};
}
} // namespace Client::Tooltips
