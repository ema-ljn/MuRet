#include "Core/Expected.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/GameHud.h"
#include "Scenes/HudItems.h"
#include "Scenes/ItemTooltipParts.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/TooltipWriter.h"
#include "Scenes/WorldState.h"
#include "World/ItemState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// 0x5b6a3c..0x5b79bc del cuerpo del ítem (06 § Los tooltips, "El cuerpo del ítem"): la durabilidad, el daño de las
// flechas, las resistencias y los requisitos contra el registro propio. Los textos son los de Text.bmd (la dirección
// − 0x76bd1dc, sobre 300).
namespace Client::Scenes
{
namespace
{
using Frame = ItemTooltipFrame;
using Own = OwnCharacterRecord;

bool InRange(std::int32_t type, std::int32_t first, std::int32_t last)
{
    return type >= first && type <= last;
}

// 0x5b6a3c..0x5b6a85: la durabilidad va con Item.bmd+0x2d o +0x2e en las armas y armaduras (hasta 0x17ff) y de 0x1a00
// a 0x1bff, y siempre en las alas (0x1800 a 0x1806); los demás, con la durabilidad del ítem distinta de 0.
constexpr std::size_t DurabilityByte = 0x2d;
constexpr std::size_t StaffDurabilityByte = 0x2e;
constexpr std::int32_t FirstWing = 0x1800;
constexpr std::int32_t LastWing = 0x1806;
constexpr std::int32_t FirstMisc = 0x1a00;
constexpr std::int32_t PotionsStart = 0x1c00;
// 0x5b6a85..0x5b6ae6: los que no la muestran.
constexpr std::int32_t FirstQuestOnly = 0x1a0e;
constexpr std::int32_t LastQuestOnly = 0x1a13;
constexpr std::uint16_t WarriorRing = 0x1a14;
constexpr std::array<std::uint16_t, 5> WithoutDurability{0x1a1d, 0x1c07, 0x1a07, 0x1a0b, 0x1a23};
// 0x5b6aec..0x5b6c3f.
constexpr std::int32_t LastPotion = 0x1c08;
constexpr std::size_t CountText = 69;
constexpr std::uint16_t LordSign = 0x1c15;
constexpr std::int32_t LordSignLevel = 3;
constexpr std::uint16_t Bolts = 0x807;
constexpr std::uint16_t Arrows = 0x80f;
constexpr std::int32_t FirstBox = 0x1a20;
constexpr std::int32_t SecondBox = 0x1a21;
constexpr std::uint16_t ThirdBox = 0x1a22;
constexpr std::size_t RatioText = 1181;
constexpr std::int32_t BoxFullCount = 0x14;
constexpr std::int32_t ThirdBoxFullCount = 0xa;
constexpr std::uint16_t BlueBox = 0x1a25;
constexpr std::int32_t LastPet = 0x1a07;
constexpr std::size_t LifeText = 70;
constexpr std::uint16_t TransformationRing = 0x1a0a;
constexpr std::size_t RingText = 95;
constexpr std::size_t DurabilityText = 71;
// 0x5b6c80..0x5b6d03: las flechas y los virotes con nivel.
constexpr std::size_t AddDamageText = 577;
constexpr std::size_t ExtraDamageText = 88;
// 0x5b6d09..0x5b6d6b: Item.bmd+0x4a a +0x50, los siete elementos (48 a 54) con el 72 y el nivel + 1.
constexpr std::size_t FirstResistanceByte = 0x4a;
constexpr std::size_t ResistanceCount = 7;
constexpr std::size_t FirstElementText = 48;
constexpr std::size_t ResistanceText = 72;
// 0x5b6d6d..0x5b79bc: los requisitos; el 74 con lo que falta.
constexpr std::size_t LevelText = 76;
constexpr std::uint16_t MonarchCrest = 0x1a0e;
constexpr std::size_t StrengthText = 73;
constexpr std::size_t AgilityText = 75;
constexpr std::size_t VitalityText = 1930;
constexpr std::size_t EnergyText = 77;
constexpr std::size_t LeadershipText = 698;
constexpr std::size_t MissingText = 74;

// Si muestra la durabilidad (0x5b6a3c..0x5b6ae6).
bool ShowsTheDurability(const Frame& frame, std::int32_t type)
{
    const bool recorded = RecordByte(frame, DurabilityByte) != 0 || RecordByte(frame, StaffDurabilityByte) != 0;
    const bool weaponOrMisc = type < FirstWing || (type >= FirstMisc && type < PotionsStart);
    const bool always = (recorded && weaponOrMisc) || InRange(type, FirstWing, LastWing);
    if (!always && frame.item.durability == 0)
        return false;
    if (InRange(type, FirstQuestOnly, LastQuestOnly))
        return false;
    if (type == WarriorRing && (frame.level == 1 || frame.level == 2))
        return false;
    for (const std::uint16_t without : WithoutDurability)
    {
        if (type == without)
            return false;
    }
    return true;
}

bool ReadsTheMaximum(const Frame& frame, std::int32_t type)
{
    const bool recorded = RecordByte(frame, DurabilityByte) != 0 || RecordByte(frame, StaffDurabilityByte) != 0;
    const bool weaponOrMisc = type < FirstWing || (type >= FirstMisc && type < PotionsStart);
    return (recorded && weaponOrMisc) || InRange(type, FirstWing, LastWing);
}

void WriteTheDurability(Frame& frame)
{
    TooltipWriter& lines = frame.lines;
    lines.At("0x5b6a3c");
    const std::int32_t type = static_cast<std::int16_t>(frame.item.id);
    const std::int32_t durability = frame.item.durability;
    if (!ShowsTheDurability(frame, type))
    {
        if (type == TransformationRing)
            lines.Line(TooltipWhite, RingText, durability);
        return;
    }
    if (InRange(type, PotionsStart, LastPotion) || (type == LordSign && frame.level == LordSignLevel) ||
        type == Bolts || type == Arrows)
        lines.Line(TooltipWhite, CountText, durability);
    else if (InRange(type, FirstBox, SecondBox))
        lines.Line(TooltipWhite, RatioText, durability, BoxFullCount);
    else if (type == ThirdBox)
        lines.Line(TooltipWhite, RatioText, durability, ThirdBoxFullCount);
    else if (type == BlueBox || InRange(type, FirstMisc, LastPet))
        lines.Line(TooltipWhite, LifeText, durability);
    else if (type == TransformationRing)
        lines.Line(TooltipWhite, RingText, durability);
    else if (ReadsTheMaximum(frame, type))
    {
        const std::uint16_t maximum = MaxDurability(frame.item, frame.record, frame.level);
        lines.Line(TooltipWhite, DurabilityText, durability, static_cast<std::int32_t>(maximum));
    }
}

void WriteTheArrows(Frame& frame)
{
    const std::uint16_t type = frame.item.id;
    if ((type != Bolts && type != Arrows) || frame.level < 1)
        return;
    TooltipWriter& lines = frame.lines;
    lines.At("0x5b6c96");
    lines.Line(TooltipBlue, AddDamageText, frame.level * 2 + 1);
    lines.Line(TooltipBlue, ExtraDamageText, 1);
}

void WriteTheResistances(Frame& frame)
{
    TooltipWriter& lines = frame.lines;
    lines.At("0x5b6d09");
    for (std::size_t i = 0; i < ResistanceCount; ++i)
    {
        if (RecordByte(frame, FirstResistanceByte + i) != 0)
            lines.Line(TooltipWhite, ResistanceText, lines.Text(FirstElementText + i), frame.level + 1);
    }
}

// Un requisito: el texto con lo que pide, y con el registro propio abierto (0x4e79a0 y 0x40f370, o el mismo código
// en línea), en rojo con el 74 y lo que falta si el stat no llega.
void WriteARequirement(Frame& frame, std::size_t text, std::uint16_t required, std::uint16_t Own::*stat,
                       std::uint16_t Own::*bonus)
{
    TooltipWriter& lines = frame.lines;
    lines.PrintText(text, static_cast<std::int32_t>(required));
    GameHudContext& context = frame.context;
    const Render::Models::LogicalAccess access(context.ownRecordDepth, context.random,
                                               Render::Models::OwnRecordCloseRandoms);
    const OwnCharacterRecord& own = context.world.OwnCharacter();
    std::uint16_t have = own.*stat;
    if (bonus != nullptr)
        have = static_cast<std::uint16_t>(have + own.*bonus);
    if (have < required)
    {
        lines.Color(TooltipRed);
        lines.Bold(0);
        lines.Next();
        lines.PrintText(MissingText, static_cast<std::int32_t>(required) - static_cast<std::int32_t>(have));
        lines.Color(TooltipRed);
    }
    else
        lines.Color(TooltipWhite);
    lines.Bold(0);
    lines.Next();
}

void WriteTheRequirementLines(Frame& frame)
{
    const World::ItemState& item = frame.item;
    frame.lines.At("0x5b6d6d");
    if (item.word28 != 0 && item.id != MonarchCrest)
        WriteARequirement(frame, LevelText, item.word28, &Own::level, nullptr);
    frame.lines.At("0x5b72b6");
    if (item.word1e != 0)
        WriteARequirement(frame, StrengthText, item.word1e, &Own::strength, &Own::word2a);
    frame.lines.At("0x5b7647");
    if (item.word20 != 0)
        WriteARequirement(frame, AgilityText, item.word20, &Own::agility, &Own::word2c);
    frame.lines.At("0x5b7726");
    if (item.word24 != 0)
        WriteARequirement(frame, VitalityText, item.word24, &Own::vitality, &Own::word2e);
    frame.lines.At("0x5b7803");
    if (item.word22 != 0)
        WriteARequirement(frame, EnergyText, item.word22, &Own::energy, &Own::word30);
    frame.lines.At("0x5b78e0");
    if (item.word26 != 0)
        WriteARequirement(frame, LeadershipText, item.word26, &Own::leadership, &Own::word36);
}
} // namespace

Core::Expected<void, std::string> WriteTheRequirements(ItemTooltipFrame& frame)
{
    WriteTheDurability(frame);
    WriteTheArrows(frame);
    WriteTheResistances(frame);
    WriteTheRequirementLines(frame);
    return {};
}
} // namespace Client::Scenes
