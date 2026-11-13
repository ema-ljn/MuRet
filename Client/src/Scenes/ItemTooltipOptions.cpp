#include "Core/Expected.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameTooltips.h"
#include "Scenes/ItemSetOptionList.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/ItemTooltipParts.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/TooltipWriter.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ItemState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

// 0x5b79bd..0x5b801a del cuerpo del ítem (06 § Los tooltips, "El cuerpo del ítem"): las clases (0x5af590), lo que
// suma cada tipo, el stat del conjunto (0x40dd80), las opciones (0x5b2320), las cajas y las opciones del conjunto
// (0x40eee0). Los textos son los de Text.bmd (la dirección − 0x76bd1dc, sobre 300).
namespace Client::Scenes
{
namespace
{
using Frame = ItemTooltipFrame;

// 0x5af590: Item.bmd+0x45..+0x49, la evolución + 1 de cada clase que lo usa (0, ninguna).
constexpr std::size_t FirstClassByte = 0x45;
constexpr std::size_t ClassCount = 5;
constexpr std::uint8_t ClassBits = 7;
constexpr std::uint8_t EvolutionShift = 3;
constexpr std::uint8_t MagicGladiator = 3;
constexpr std::size_t MagicGladiatorByte = 0x48;
// 0x5af590: el 60 con el nombre de la clase es "no la usa" y el 61, "la usa"; los nombres van del 15 + 5 · el byte
// + la clase, y del 20 + la clase sin la evolución; el 23, el de MG.
constexpr std::size_t CannotText = 60;
constexpr std::size_t CanText = 61;
constexpr std::size_t FirstClassNameText = 15;
constexpr std::size_t PlainClassNameText = 20;
constexpr std::size_t ClassNameStep = 5;
constexpr std::size_t MagicGladiatorText = 23;
// 0x5af5e9 y 0x5af5f0: los colores de N + 2 y N + 3 en 0.
constexpr std::int32_t FirstClearedAhead = 2;
constexpr std::int32_t LastClearedAhead = 3;

// 0x5b79df..0x5b7c37: lo que suma cada tipo, con su nivel desde 5 en los cascos y las botas.
constexpr std::int32_t FirstHelm = 0x1400;
constexpr std::int32_t FirstBoots = 0x1600;
constexpr std::int32_t BootsEnd = 0x1800;
constexpr std::int32_t BonusLevel = 5;
constexpr std::size_t BootsText = 78;
constexpr std::size_t HelmText = 93;
constexpr std::int32_t FirstShield = 0xa00;
constexpr std::int32_t ShieldsEnd = 0xc00;
constexpr std::array<std::int32_t, 3> WithBlocking{0x1f, 0x17, 0x15};
constexpr std::size_t BlockingText = 79;
constexpr std::int32_t FirstWand = 0x408;
constexpr std::int32_t LastWand = 0x40e;
constexpr std::size_t WandText = 1234;

// 0x40dd80 y 0x40dca0: D+0x1c, (>> 2) & 3, por 5 en el 950 + Item.bmd+0x44 − 1; el 1165 en las joyas y los anillos.
constexpr std::size_t SetStatByte = 0x44;
constexpr std::size_t FirstSetStatText = 950;
constexpr std::int32_t SetStatKinds = 4;
constexpr std::int32_t SetStatStep = 5;
constexpr std::size_t SetRingText = 1165;
constexpr std::array<std::array<std::int32_t, 2>, 3> SetRingRanges{
    {{0x1a08, 0x1a09}, {0x1a0c, 0x1a0d}, {0x1a15, 0x1a1b}}};

// 0x5b7c45..0x5b7c5d: 0x551c70 con la skill del byte D+0x2b + 7 (el contador de las resistencias, [EBP+0x14], queda
// en 7).
constexpr std::size_t LastOptionSlot = 7;

// 0x5b2320(tipo, renglón, opción, valor, maná): la tabla de 0x5b2a00 y 0x5b2938.
enum class OptionArgument : std::uint8_t
{
    None,
    Value,
    Mana,
};

struct OptionText
{
    std::uint8_t tag = 0;
    std::uint16_t text = 0;
    OptionArgument argument = OptionArgument::None;
};

constexpr std::array<OptionText, 49> OptionTexts{{
    {0x12, 80, OptionArgument::Mana},    {0x13, 81, OptionArgument::Mana},   {0x14, 82, OptionArgument::Mana},
    {0x15, 83, OptionArgument::Mana},    {0x16, 84, OptionArgument::Mana},   {0x17, 85, OptionArgument::Mana},
    {0x18, 86, OptionArgument::Mana},    {0x31, 745, OptionArgument::Mana},  {0x36, 920, OptionArgument::Mana},
    {0x38, 98, OptionArgument::Mana},    {0x3c, 1210, OptionArgument::Mana}, {0x3e, 1189, OptionArgument::Mana},
    {0x42, 1186, OptionArgument::Mana},  {0x4c, 1928, OptionArgument::Mana}, {0x50, 88, OptionArgument::Value},
    {0x51, 89, OptionArgument::Value},   {0x52, 90, OptionArgument::Value},  {0x53, 91, OptionArgument::Value},
    {0x54, 87, OptionArgument::None},    {0x55, 92, OptionArgument::Value},  {0x56, 622, OptionArgument::None},
    {0x57, 623, OptionArgument::None},   {0x58, 624, OptionArgument::None},  {0x59, 625, OptionArgument::None},
    {0x5a, 626, OptionArgument::None},   {0x5b, 627, OptionArgument::None},  {0x5c, 628, OptionArgument::None},
    {0x5d, 629, OptionArgument::None},   {0x5e, 630, OptionArgument::Value}, {0x5f, 631, OptionArgument::None},
    {0x60, 632, OptionArgument::Value},  {0x61, 633, OptionArgument::Value}, {0x62, 634, OptionArgument::None},
    {0x63, 635, OptionArgument::None},   {0x64, 740, OptionArgument::Value}, {0x65, 741, OptionArgument::Value},
    {0x66, 742, OptionArgument::Value},  {0x67, 743, OptionArgument::Value}, {0x68, 744, OptionArgument::Value},
    {0x6e, 746, OptionArgument::None},   {0x8c, 985, OptionArgument::Value}, {0x8d, 986, OptionArgument::Value},
    {0x8e, 988, OptionArgument::Value},  {0x8f, 987, OptionArgument::Value}, {0x90, 954, OptionArgument::Value},
    {0x94, 577, OptionArgument::Value},  {0x96, 959, OptionArgument::Value}, {0xb0, 1087, OptionArgument::Value},
    {0xb1, 1088, OptionArgument::Value},
}};
// 0x5b255b: la 0x55 no escribe nada en los ítems de misión de 0x1a0e a 0x1a12.
constexpr std::uint8_t LuckOption = 0x55;
constexpr std::int32_t FirstQuestItem = 0x1a0e;
constexpr std::int32_t LastQuestItem = 0x1a12;

// 0x5b7ccc..0x5b7d8f: el renglón de más de algunas opciones.
constexpr std::uint8_t LifeOption = 0x54;
constexpr std::size_t LifeText = 94;
constexpr std::uint8_t ShieldSkillOption = 0x31;
constexpr std::size_t ShieldSkillText = 179;
constexpr std::uint8_t FireScreamOption = 0x3e;
constexpr std::size_t FireScreamText = 1201;
constexpr std::uint8_t AddDefenseOption = 0x50;
constexpr std::size_t AddDefenseText = 89;

// 0x5b7de1..0x5b800d: las cajas.
constexpr std::int32_t FirstBox = 0x1a20;
constexpr std::int32_t SecondBox = 0x1a21;
constexpr std::int32_t ThirdBox = 0x1a22;
constexpr std::int32_t FourthBox = 0x1a23;
constexpr std::int32_t FifthBox = 0x1a24;
constexpr std::int32_t BlueBox = 0x1a25;
constexpr std::size_t FirstBoxText = 1917;
constexpr std::size_t ThirdBoxText = 1918;
constexpr std::size_t FifthBoxText = 1919;
constexpr std::uint8_t BlueBoxOption = 0x4c;
constexpr std::size_t FirstBlueText = 1860;
constexpr std::size_t SecondBlueText = 1861;
constexpr std::int32_t BlueBoxChance = 0xa;
constexpr std::size_t BlueTailText = 579;
constexpr std::size_t BlueBoxText = 1920;
constexpr std::size_t EmptyBlueBoxText = 1929;

// 0x40eee0: ItemSetOption.bmd (05, Las tablas de los conjuntos).
constexpr std::uint8_t SetKindMask = 3;
constexpr std::size_t KindZeroByte = 3;
constexpr std::uint8_t NoSet = 0xff;
constexpr std::size_t SetUsedByte = 0x3c;
constexpr std::size_t SetPairOptions = 0x20;
constexpr std::size_t SetPairValues = 0x2c;
constexpr std::size_t SetPairCount = 6;
constexpr std::size_t SetExtraEnd = 8;
constexpr std::size_t SetExtraOptions = 0x32;
constexpr std::size_t SetExtraValues = 0x34;
constexpr std::size_t SetFullOptions = 0x35;
constexpr std::size_t SetFullValues = 0x3a;
constexpr std::size_t SetSlots = 13;
constexpr std::uint8_t FullSetActive = 0xd;
constexpr std::size_t SetTitleText = 1089;
constexpr std::size_t SetTailText = 159;

std::uint8_t HeroClassOf(const GameHudContext& context)
{
    const WorldState& world = context.world;
    return world.Actors()[world.Runtime().hero].characterClass;
}

// 0x5af5e4..0x5af5f0: los colores de N + 2 y N + 3 en 0, aunque no escriba esos renglones.
void ClearTheColorsAhead(TooltipWriter& lines)
{
    if (lines.Failed())
        return;
    const std::int32_t next = lines.Count();
    if (next < 0 || next + LastClearedAhead >= static_cast<std::int32_t>(TooltipLineCount))
    {
        lines.Fail(
            std::format("0x5af5e9: el color del renglón {} cae fuera de los 30 de 0x787a560", next + LastClearedAhead));
        return;
    }
    TooltipLines& written = lines.Lines();
    written.colors[static_cast<std::size_t>(next + LastClearedAhead)] = TooltipWhite;
    written.colors[static_cast<std::size_t>(next + FirstClearedAhead)] = TooltipWhite;
}

// Un byte de las clases del registro de Item.bmd.
std::uint8_t ClassByte(std::span<const std::byte, Data::Tables::ItemRecordSize> record, std::size_t offset)
{
    return std::to_integer<std::uint8_t>(record[offset]);
}
} // namespace

// 0x5af590(registro), de los dos que la llaman (ItemTooltipParts.h). `other` es el color de lo que el héroe no cumple:
// 2 si su clase lo usa (Item.bmd+0x45 + clase distinto de 0) y 5 si no.
void WriteTheItemClasses(const GameHudContext& context, TooltipWriter& lines,
                         std::span<const std::byte, Data::Tables::ItemRecordSize> record)
{
    lines.At("0x5af590");
    const std::uint8_t hero = HeroClassOf(context);
    const std::uint8_t heroClass = hero & ClassBits;
    const std::int32_t heroEvolution = hero >> EvolutionShift;
    const std::int32_t other = ClassByte(record, FirstClassByte + heroClass) != 0 ? TooltipRed : TooltipWhiteOnBlue;
    std::array<std::int32_t, ClassCount> classes{};
    std::array<std::int32_t, ClassCount> evolutions{};
    std::size_t count = 0;
    for (std::size_t i = 0; i < ClassCount; ++i)
    {
        const std::uint8_t value = ClassByte(record, FirstClassByte + i);
        if (value == 0)
            continue;
        classes[count] = static_cast<std::int32_t>(i);
        evolutions[count] = value - 1;
        ++count;
    }
    ClearTheColorsAhead(lines);
    if (count < 1)
        return;
    if (heroClass == MagicGladiator)
    {
        // 0x5af60c..0x5af6e9: el MG solo mira su byte, con cualquier cantidad de clases.
        lines.NewLine();
        const std::uint8_t mg = ClassByte(record, MagicGladiatorByte);
        if (mg != 0 && heroEvolution >= mg - 1)
        {
            lines.PrintText(CanText, lines.Text(MagicGladiatorText));
            lines.Color(TooltipWhite);
        }
        else
        {
            lines.PrintText(CannotText, lines.Text(MagicGladiatorText));
            lines.Color(other);
        }
        lines.Bold(0);
        lines.Next();
        return;
    }
    if (count > 4)
        return;
    lines.NewLine();
    const auto fits = [&](std::size_t entry)
    { return heroClass == classes[entry] && heroEvolution >= evolutions[entry]; };
    const auto nameOf = [&](std::size_t entry)
    {
        const auto byClass = static_cast<std::size_t>(classes[entry]);
        const std::size_t value = ClassByte(record, FirstClassByte + byClass);
        return lines.Text(FirstClassNameText + ClassNameStep * value + byClass);
    };
    if (count <= 2)
    {
        // 0x5af72f..0x5af8ce: una o dos clases, cada una con su nombre; en blanco si el héroe cumple alguna.
        const bool any = fits(0) || (count == 2 && fits(1));
        for (std::size_t entry = 0; entry < count; ++entry)
        {
            lines.Color(any ? TooltipWhite : other);
            lines.PrintText(CanText, nameOf(entry));
            lines.Bold(0);
            lines.Next();
        }
        return;
    }
    // 0x5af8d3..0x5af92d: con tres o cuatro, las que no lo usan.
    for (std::size_t i = 0; i < ClassCount; ++i)
    {
        if (ClassByte(record, FirstClassByte + i) != 0)
            continue;
        lines.Color(other);
        lines.PrintText(CannotText, lines.Text(PlainClassNameText + i));
        lines.Bold(0);
        lines.Next();
    }
}

namespace
{
// Un renglón azul y en negrita después de un "\n" (el 78 y el 93 van sin argumentos).
template <typename... Arguments> void WriteABonus(TooltipWriter& lines, std::size_t text, Arguments... arguments)
{
    lines.NewLine();
    lines.PrintText(text, arguments...);
    lines.Color(TooltipBlue);
    lines.Bold(1);
    lines.Next();
}

void WriteTheBonuses(Frame& frame)
{
    TooltipWriter& lines = frame.lines;
    lines.At("0x5b79df");
    const World::ItemState& item = frame.item;
    const std::int32_t type = static_cast<std::int16_t>(item.id);
    const std::int32_t level = frame.level;
    if (type >= FirstBoots && type < BootsEnd && level >= BonusLevel)
        WriteABonus(lines, BootsText);
    if (type >= FirstHelm && type < FirstBoots && level >= BonusLevel)
        WriteABonus(lines, HelmText);
    if ((type >= FirstShield && type < ShieldsEnd) || std::ranges::find(WithBlocking, type) != WithBlocking.end())
        WriteABonus(lines, BlockingText, static_cast<std::int32_t>(item.byte16));
    if (type >= FirstWand && type <= LastWand)
        WriteABonus(lines, WandText, static_cast<std::int32_t>(item.byte16));
    if (item.optionCount > 0)
        lines.NewLine();
}

// 0x40dd80(ítem, N), un thiscall sobre [0x6eed3c], con 0x40dca0: sin negrita.
void WriteTheSetStat(Frame& frame)
{
    TooltipWriter& lines = frame.lines;
    lines.At("0x40dd80");
    const World::ItemState& item = frame.item;
    const std::int32_t kind = (item.flags1c >> 2) & SetKindMask;
    if (kind <= 0)
        return;
    const std::int32_t stat = RecordByte(frame, SetStatByte) - 1;
    if (stat < 0 || stat >= SetStatKinds)
        return;
    lines.PrintText(FirstSetStatText + static_cast<std::size_t>(stat), SetStatStep * kind);
    lines.Color(TooltipBlue);
    lines.Next();
    const std::int32_t type = static_cast<std::int16_t>(item.id);
    const bool ring = std::ranges::any_of(SetRingRanges, [type](const std::array<std::int32_t, 2>& range)
                                          { return type >= range[0] && type <= range[1]; });
    if (!ring)
        return;
    lines.PrintText(SetRingText);
    lines.Color(TooltipBlue);
    lines.Next();
}

// 0x5b2320(tipo, renglón, opción, valor, maná): el texto de la opción en el renglón que sigue, sin su color ni su
// negrita. Las que leen el maná abren la entrada de la skill de la opción (0x551c70, que pisa el maná del argumento).
// Una opción que no está en la tabla deja el renglón como estaba.
Core::Expected<void, std::string> WriteAnOption(Frame& frame, std::int32_t type, std::uint8_t tag, std::uint8_t value)
{
    TooltipWriter& lines = frame.lines;
    const auto found = std::ranges::find(OptionTexts, tag, &OptionText::tag);
    if (found == OptionTexts.end())
        return {};
    if (tag == LuckOption && type >= FirstQuestItem && type <= LastQuestItem)
        return {};
    switch (found->argument)
    {
    case OptionArgument::None:
        lines.PrintText(found->text);
        break;
    case OptionArgument::Value:
        lines.PrintText(found->text, static_cast<std::int32_t>(value));
        break;
    case OptionArgument::Mana:
    {
        const auto entry = ReadTheSkillEntry(frame, tag, "0x5b2356");
        if (!entry.HasValue())
            return Core::Unexpected(entry.Error());
        lines.PrintText(found->text, static_cast<std::int32_t>(entry.Value().mana));
        break;
    }
    }
    return {};
}

// 0x5b7c62..0x5b7d9f: cada opción en azul, con el renglón de más de algunas.
Core::Expected<void, std::string> WriteTheOptionLines(Frame& frame)
{
    TooltipWriter& lines = frame.lines;
    const World::ItemState& item = frame.item;
    const std::int32_t type = static_cast<std::int16_t>(item.id);
    for (std::size_t i = 0; i < item.optionCount; ++i)
    {
        lines.At("0x5b7c8a");
        if (i >= World::MaxItemOptions)
            return Core::Unexpected(
                std::format("0x5b7c8d: la opción {} de {} lee su valor fuera de D+0x2b..D+0x3b", i, item.optionCount));
        const std::uint8_t tag = item.optionBytes[i];
        const std::uint8_t value = item.optionBytes[World::ItemOptionValueOffset + i];
        if (auto written = WriteAnOption(frame, type, tag, value); !written.HasValue())
            return written;
        lines.Color(TooltipBlue);
        lines.Bold(0);
        lines.Next();
        if (tag == LifeOption)
            lines.Line(TooltipBlue, LifeText, static_cast<std::int32_t>(value));
        else if (tag == ShieldSkillOption)
            lines.Line(TooltipWhiteOnBlue, ShieldSkillText);
        else if (tag == FireScreamOption)
            lines.Line(TooltipWhiteOnBlue, FireScreamText);
        else if (tag == AddDefenseOption && std::ranges::find(WithBlocking, type) != WithBlocking.end())
            lines.Line(TooltipBlue, AddDefenseText, static_cast<std::int32_t>(value));
    }
    return {};
}

// 0x5b7de1..0x5b800d: las cajas, sin negrita salvo el primer renglón de la azul.
Core::Expected<void, std::string> WriteTheBoxes(Frame& frame)
{
    TooltipWriter& lines = frame.lines;
    lines.At("0x5b7de1");
    const World::ItemState& item = frame.item;
    const std::int32_t type = static_cast<std::int16_t>(item.id);
    const auto gold = [&lines](std::size_t text)
    {
        lines.PrintText(text);
        lines.Color(TooltipGold);
        lines.Next();
    };
    if (type == FirstBox || type == SecondBox)
        gold(FirstBoxText);
    else if (type == ThirdBox || type == FourthBox)
        gold(ThirdBoxText);
    else if (type == FifthBox)
        gold(FifthBoxText);
    else if (type == BlueBox)
    {
        // 0x5b7e49..0x5b7e8b: 0x5b2320(0, renglón, 0x4c, 0, 0), y el renglón suma también un medio.
        if (auto written = WriteAnOption(frame, 0, BlueBoxOption, 0); !written.HasValue())
            return written;
        lines.Color(TooltipBlue);
        lines.Bold(0);
        lines.Next();
        lines.AddHalf();
        const std::uint8_t kind = item.flags1b;
        if (kind == 1 || kind == 2)
        {
            lines.PrintText(kind == 1 ? FirstBlueText : SecondBlueText, BlueBoxChance);
            lines.Color(TooltipBlue);
            lines.Next();
            lines.PrintText(BlueTailText);
            lines.Color(TooltipBlue);
            lines.Next();
        }
        lines.NewLine();
        gold(BlueBoxText);
        if (kind == 0)
            gold(EmptyBlueBoxText);
    }
    return {};
}

std::string_view UpToTheZero(std::span<const char> bytes)
{
    const auto end = std::ranges::find(bytes, '\0');
    return {bytes.data(), static_cast<std::size_t>(end - bytes.begin())};
}

// Un renglón de 0x40eee0: la opción con su valor (0x40d530), en azul si es la que sigue de la lista del registro
// propio y el conjunto la tiene puesta; si no, en gris.
Core::Expected<void, std::string> WriteASetOption(Frame& frame, std::uint8_t option, std::uint8_t value,
                                                  std::size_t slot, std::uint8_t active, std::uint8_t& start)
{
    TooltipWriter& lines = frame.lines;
    const ItemSetOptionList& list = frame.context.world.ItemSets().list;
    std::string* line = lines.Current();
    if (line == nullptr)
        return Core::Unexpected(lines.Error());
    if (auto written = WriteTheSetOption(lines.Texts(), *line, option, value); !written.HasValue())
        return written;
    if (start >= ItemSetOptionCapacity)
        return Core::Unexpected(std::format("0x40f160: las opciones del conjunto leen la entrada {} de 16 (las marcas "
                                            "de +0x937c como opciones)",
                                            start));
    if (list.entries[start][0] == option && slot < active && active != NoSet)
    {
        lines.Color(TooltipBlue);
        ++start;
    }
    else
        lines.Color(TooltipGray);
    lines.Bold(0);
    lines.Next();
    return {};
}

// 0x40eee0(ítem, N), un thiscall sobre [0x6eed3c]: con un conjunto en uso, su título y sus opciones, en azul las que
// tiene puestas el héroe (la lista de +0x934c, desde la clase del conjunto: +0x9308 o +0x9328) y en gris las demás.
Core::Expected<void, std::string> WriteTheSetOptions(Frame& frame)
{
    TooltipWriter& lines = frame.lines;
    lines.At("0x40eee0");
    if (lines.Failed())
        return Core::Unexpected(lines.Error());
    GameHudContext& context = frame.context;
    const World::ItemState& item = frame.item;
    ItemSetState& sets = context.world.ItemSets();
    if (context.setTypes == nullptr || context.setOptions == nullptr)
        return Core::Unexpected(std::string("0x40ef05: las opciones del conjunto sin ItemSetType.bmd ni "
                                            "ItemSetOption.bmd"));
    // 0x40eefe..0x40ef05: el byte & 3 − 1 del tipo; con & 3 en 0, el byte 3 del tipo anterior, y en el tipo 0 el byte
    // alto de la vtable de +0 (0x6b8824, 0x40f2c8): 0.
    const auto kind = static_cast<std::size_t>(item.flags1c & SetKindMask);
    std::uint8_t set = 0;
    if (kind != 0 || item.id != 0)
    {
        const std::size_t type = kind != 0 ? item.id : item.id - 1u;
        const auto record = context.setTypes->Record(type);
        if (!record)
            return Core::Unexpected(std::format("0x40ef05: el tipo {:#x} lee fuera de ItemSetType.bmd", type));
        set = std::to_integer<std::uint8_t>((*record)[kind != 0 ? kind - 1 : KindZeroByte]);
    }
    if (!context.projecting)
        sets.byte9306 = set;
    if (set == 0 || set == NoSet)
        return {};
    const auto found = context.setOptions->Record(set);
    if (!found)
        return Core::Unexpected(std::format("0x40ef22: el conjunto {} lee fuera de los 64 de ItemSetOption.bmd", set));
    const std::span<const std::byte, Data::Tables::ItemSetOptionRecordSize> record = *found;
    const auto byteAt = [&record](std::size_t offset) { return std::to_integer<std::uint8_t>(record[offset]); };
    if (byteAt(SetUsedByte) == NoSet)
    {
        if (!context.projecting)
            sets.byte9306 = 0;
        return {};
    }
    // 0x40ef48..0x40f005: la clase del conjunto. strlen del nombre del registro, que sigue por sus opciones si el
    // nombre no tiene su 0, y de los dos nombres de +0x9308 y +0x9328.
    const auto zero = std::ranges::find(record, std::byte{0});
    if (zero == record.end())
        return Core::Unexpected(std::format("0x40ef68: el registro del conjunto {} no tiene un 0 (strlen sigue por el "
                                            "conjunto siguiente)",
                                            set));
    std::string name;
    for (auto at = record.begin(); at != zero; ++at)
        name.push_back(static_cast<char>(std::to_integer<unsigned char>(*at)));
    for (std::size_t i = 0; i < ItemSetKindCount; ++i)
    {
        if (std::ranges::find(sets.names[i], '\0') == sets.names[i].end())
            return Core::Unexpected(std::format("0x40ef75: el nombre {} de +0x9308 no tiene su 0", i));
    }
    const std::string_view first = UpToTheZero(sets.names[0]);
    const std::string_view second = UpToTheZero(sets.names[1]);
    const ItemSetOptionList& list = sets.list;
    // strncmp(a, b, strlen(a)) da 0 si b empieza con a.
    auto active = static_cast<std::uint8_t>(list.count9348 - sets.byte9307);
    std::uint8_t start = 0;
    if (second.starts_with(name) && std::string_view(name).starts_with(second))
    {
        start = static_cast<std::uint8_t>(list.count9348 - sets.byte9307);
        active = list.count9348;
    }
    else if (!first.starts_with(name) && !std::string_view(name).starts_with(first))
        active = 0;
    // 0x40f009..0x40f090: sin negritas ni medios.
    lines.Print("\n");
    lines.Next();
    lines.Print("%s %s", lines.Text(SetTitleText), lines.Text(SetTailText));
    lines.Color(TooltipGold);
    lines.Next();
    lines.Print("\n");
    lines.Next();
    lines.Print("\n");
    lines.Next();
    // 0x40f0c0..0x40f255: después del sexto par la segunda opción queda la de ese par (Q-C592).
    std::uint8_t secondOption = NoSet;
    std::uint8_t secondValue = NoSet;
    for (std::size_t slot = 0; slot < SetSlots; ++slot)
    {
        std::uint8_t firstOption = 0;
        std::uint8_t firstValue = 0;
        if (slot < SetPairCount)
        {
            firstOption = byteAt(SetPairOptions + 2 * slot);
            secondOption = byteAt(SetPairOptions + 2 * slot + 1);
            firstValue = byteAt(SetPairValues + 2 * slot);
            secondValue = byteAt(SetPairValues + 2 * slot + 1);
        }
        else if (slot < SetExtraEnd)
        {
            firstOption = byteAt(SetExtraOptions + slot);
            firstValue = byteAt(SetExtraValues + slot);
        }
        else
        {
            firstOption = byteAt(SetFullOptions + slot);
            firstValue = byteAt(SetFullValues + slot);
            if (active != NoSet)
            {
                if (start >= ItemSetOptionCapacity)
                    return Core::Unexpected(std::format("0x40f117: las opciones del conjunto leen la entrada {} de 16 "
                                                        "(las marcas de +0x937c como opciones)",
                                                        start));
                active = list.entries[start][0] == firstOption ? FullSetActive : NoSet;
            }
        }
        if (firstOption != NoSet)
        {
            if (auto written = WriteASetOption(frame, firstOption, firstValue, slot, active, start);
                !written.HasValue())
                return written;
        }
        if (secondOption != NoSet)
        {
            if (auto written = WriteASetOption(frame, secondOption, secondValue, slot, active, start);
                !written.HasValue())
                return written;
        }
    }
    // 0x40f25b..0x40f292: dos "\n" sin medios; devuelve N + 2.
    lines.Print("\n");
    lines.Next();
    lines.Print("\n");
    lines.Next();
    return {};
}
} // namespace

Core::Expected<void, std::string> WriteTheOptions(ItemTooltipFrame& frame)
{
    const World::ItemState& item = frame.item;
    constexpr std::uint16_t AngelQuestItem = 0x1a13;
    constexpr std::uint16_t SealedItem = 0x1a1d;
    if (item.id != AngelQuestItem && item.id != SealedItem)
        WriteTheItemClasses(frame.context, frame.lines, frame.record);
    WriteTheBonuses(frame);
    WriteTheSetStat(frame);
    frame.lines.At("0x5b7c5d");
    if (auto entry = ReadTheSkillEntry(frame, item.optionBytes[LastOptionSlot], "0x5b7c5d"); !entry.HasValue())
        return Core::Unexpected(entry.Error());
    if (auto written = WriteTheOptionLines(frame); !written.HasValue())
        return written;
    frame.lines.At("0x5b7da7");
    frame.lines.NewLine();
    if (auto written = WriteTheBoxes(frame); !written.HasValue())
        return written;
    return WriteTheSetOptions(frame);
}
} // namespace Client::Scenes
