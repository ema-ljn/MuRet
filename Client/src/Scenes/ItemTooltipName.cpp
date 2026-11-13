#include "Core/Expected.h"
#include "Scenes/HudItems.h"
#include "Scenes/ItemTooltipParts.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/TooltipWriter.h"
#include "World/ItemState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

// 0x5b371b..0x5b45f4, el nombre del cuerpo del ítem (06 § Los tooltips, "El cuerpo del ítem"). Los textos son los de
// Text.bmd (la dirección − 0x76bd1dc, sobre 300).
namespace Client::Scenes
{
namespace
{
using Frame = ItemTooltipFrame;

// Un caso del nombre por nivel: el texto de Text.bmd como formato, el nombre del registro con "%s" o nada (el
// renglón queda vacío y la caja termina ahí).
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
// 0x1a14 con nivel 0 (0x5b3b91): sprintf con el nombre del registro como formato.
constexpr NameCase NameAsFormat{.kind = NameKind::RecordAsFormat};
constexpr NameCase TextName(std::size_t text)
{
    return NameCase{.kind = NameKind::Text, .text = text};
}

// Los tipos con un nombre por nivel (0x5b3737..0x5b3cf9), del nivel 0 en adelante; más allá, NoName.
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

// 0x1c0b, la tabla de 0x5b80a8: del nivel 8 al 12, "%s +%d" con el 115 y el nivel − 7.
constexpr std::uint16_t LuckyBox = 0x1c0b;
constexpr std::array<NameCase, 16> LuckyBoxNames{
    Named,  TextName(105), TextName(106), TextName(107), NoName, TextName(109), TextName(110),  TextName(111),
    NoName, NoName,        NoName,        NoName,        NoName, TextName(117), TextName(1650), TextName(1651)};
constexpr std::int32_t FirstNumberedLuckyBox = 8;
constexpr std::int32_t LastNumberedLuckyBox = 12;
constexpr std::int32_t LuckyBoxNumberBase = 7;
constexpr std::size_t LuckyBoxText = 115;
// 0x1c0c con nivel 2: el texto de la tabla de .data 0x6c9350 (100 bytes por durabilidad).
constexpr std::uint16_t EventTicket = 0x1c0c;
constexpr std::int32_t EventTicketTableLevel = 2;
// 0x1c19 y 0x1c1a: "%s" en dorado.
constexpr std::uint16_t FirstGoldQuest = 0x1c19;
constexpr std::uint16_t LastGoldQuest = 0x1c1a;
// 0x1a0f: "%s %s" con el stat del nivel (0 a 4) y el nombre.
constexpr std::uint16_t StatFruit = 0x1a0f;
constexpr std::array<std::size_t, 5> StatTexts{168, 169, 167, 166, 1900};
// 0x180b: "%s %s" con el nombre de la entrada 30 + nivel de Skill.bmd y el 102.
constexpr std::uint16_t SkillJewel = 0x180b;
constexpr std::uint32_t FirstJewelSkill = 30;
constexpr std::size_t JewelText = 102;
// 0x1a0a busca en la tabla 0x7681eb8 (de a 0x36 bytes hasta 0x7688ab8) el word de 0x6c9338[nivel].
constexpr std::uint16_t TransformationRing = 0x1a0a;
// 0x1a1f: "%s %s" con el 1187 o el 1214 delante.
constexpr std::uint16_t PetSpirit = 0x1a1f;
constexpr std::array<std::size_t, 2> PetSpiritTexts{1187, 1214};
// 0x1c1d: el 1180 con el nivel; 0x1c1c, "%s +%d" en dorado.
constexpr std::uint16_t MarkOfKundun = 0x1c1d;
constexpr std::size_t MarkOfKundunText = 1180;
constexpr std::uint16_t LostMap = 0x1c1c;
// 0x1c20 a 0x1c22: "%s".
constexpr std::uint16_t FirstSiegePotion = 0x1c20;
constexpr std::uint16_t LastSiegePotion = 0x1c22;
// 0x1a20 a 0x1a24 en blanco y 0x1a25 en azul, con el 1863 o el 1864 detrás según D+0x1b & 0x3f.
constexpr std::uint16_t FirstBox = 0x1a20;
constexpr std::uint16_t LastWhiteBox = 0x1a24;
constexpr std::uint16_t BlueBox = 0x1a25;
constexpr std::array<std::size_t, 2> BlueBoxTexts{1863, 1864};
// Los que van con "%s" en el nivel 0 y "%s +%d" en los demás (0x5b4563): las alas de 0x1803 a 0x1806, la capa
// 0x1a1e y los cuatro de 0x5b43e1.
constexpr std::uint16_t FirstHighWing = 0x1803;
constexpr std::uint16_t LastHighWing = 0x1806;
constexpr std::uint16_t Cape = 0x1a1e;
constexpr std::array<std::uint16_t, 4> PlusNamed{0x13, 0x812, 0xa0a, 0x40d};
// Los paquetes de Bless y de Soul: "%s +%d" con el 1806 o el 1807 y el nivel + 1.
constexpr std::uint16_t BlessBundle = 0x181e;
constexpr std::uint16_t SoulBundle = 0x181f;
constexpr std::size_t BlessText = 1806;
constexpr std::size_t SoulText = 1807;
// El genérico: el 620 delante con alguna opción excelente; [EBP−0x6c] es de 64 bytes.
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

// "%s" en el nivel 0 y "%s +%d" en los demás.
void WritePlusName(TooltipWriter& lines, std::string_view name, std::int32_t level)
{
    if (level == 0)
        lines.Print("%s", name);
    else
        lines.Print("%s +%d", name, level);
}

// 0x5b3737..0x5b3cf9 y 0x5b3881..0x5b39a7: los de un nombre por nivel. false si `type` no es uno de ellos.
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
    if (type == EventTicket && level == EventTicketTableLevel)
        return Core::Unexpected(std::format("0x5b3824: el nombre del 0x1c0c con nivel 2 sale de la tabla de .data "
                                            "0x6c9350 (la durabilidad {} · 100), que MuRet no tiene",
                                            frame.item.durability));
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

// 0x5b3d09..0x5b41cb: el nombre de la entrada 30 + nivel de Skill.bmd con el acceso abierto y el 102.
Core::Expected<void, std::string> WriteTheJewelName(Frame& frame)
{
    const auto entry = ReadTheSkillEntry(frame, FirstJewelSkill + static_cast<std::uint32_t>(frame.level), "0x5b3d09");
    if (!entry.HasValue())
        return Core::Unexpected(entry.Error());
    frame.lines.Print("%s %s", std::string_view(entry.Value().name), frame.lines.Text(JewelText));
    return {};
}

// 0x5b4251..0x5b445e: las mascotas, los sellos, las pociones de asedio, las cajas, los de "+%d" y los paquetes.
// false si `type` no es uno de ellos.
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

// 0x5b445e..0x5b4590: el genérico, con el nombre del conjunto (0x40cf30) delante y el 620 con alguna excelente.
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
        return Core::Unexpected(std::string("0x5b41dc: el nombre del 0x1a0a busca su monstruo en la tabla 0x7681eb8, "
                                            "que MuRet no tiene"));
    if (WriteASpecialName(frame, type, record))
        return {};
    return WriteTheGenericName(frame, record);
}
} // namespace

Core::Expected<void, std::string> WriteTheItemName(ItemTooltipFrame& frame)
{
    frame.lines.At("0x5b371b");
    const std::string record = ItemRecordName(frame.record);
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
} // namespace Client::Scenes
