#include "Core/Expected.h"
#include "Scenes/ItemTooltipParts.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/TooltipWriter.h"
#include "World/ItemState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// 0x5b58ea..0x5b6a3b del cuerpo del ítem (06 § Los tooltips, "El cuerpo del ítem"): los paquetes de joyas, el texto
// de cada joya y de cada objeto de evento, las alas y las capas. Los textos son los de Text.bmd (la dirección −
// 0x76bd1dc, sobre 300).
namespace Client::Scenes
{
namespace
{
using Frame = ItemTooltipFrame;

bool InRange(std::int32_t type, std::int32_t first, std::int32_t last)
{
    return type >= first && type <= last;
}

// 0x402b30 y 0x402d30: los paquetes de Bless (2) y de Soul (3) cuentan (5 · nivel + 5) · 2 joyas, en un byte con
// signo: del nivel 12 en adelante da negativo y no hay renglón.
constexpr std::uint16_t BlessBundle = 0x181e;
constexpr std::uint16_t SoulBundle = 0x181f;
constexpr std::size_t BundleText = 1819;
constexpr std::size_t UnbundleText = 1820;
constexpr std::size_t BlessText = 1806;
constexpr std::size_t SoulText = 1807;

// 0x5b5a11..0x5b5c60: un texto suelto por tipo, en blanco.
struct TypeText
{
    std::uint16_t type = 0;
    std::size_t text = 0;
};

constexpr std::array<TypeText, 8> TypeTexts{{{0x1c0d, 572},
                                             {0x1c0e, 573},
                                             {0x1c10, 621},
                                             {0x1c11, 637},
                                             {0x1c12, 637},
                                             {0x180f, 574},
                                             {0x1c16, 619},
                                             {0x1c1f, 1289}}};
constexpr std::uint16_t Guardian = 0x1a00;
constexpr std::size_t AbsorbText = 578;
constexpr std::int32_t GuardianAbsorb = 0x14;
constexpr std::size_t MaxLifeText = 739;
constexpr std::int32_t GuardianLife = 0x32;
constexpr std::uint16_t Imp = 0x1a01;
constexpr std::size_t ImpText = 576;

// 0x5b5c66..0x5b5e2a: las alas y el 0x1a03 con el daño que suman, el que absorben y el 579.
constexpr std::int32_t FirstWing = 0x1800;
constexpr std::int32_t LastWing = 0x1802;
constexpr std::int32_t FirstHighWing = 0x1803;
constexpr std::int32_t LastHighWing = 0x1806;
constexpr std::size_t AddDamageText = 577;
constexpr std::size_t SpeedText = 579;
constexpr std::int32_t WingBase = 0xc;
constexpr std::int32_t HighWingDamageBase = 0x20;
constexpr std::int32_t HighWingAbsorbBase = 0x19;
constexpr std::uint16_t Dinorant = 0x1a03;
constexpr std::int32_t DinorantDamage = 0xf;
constexpr std::int32_t DinorantAbsorb = 0xa;

// 0x5b5e34..0x5b6394.
constexpr std::uint16_t PetSpirit = 0x1a1f;
constexpr std::array<std::size_t, 2> PetSpiritTexts{1215, 1216};
constexpr std::uint16_t MonarchCrest = 0x1a0e;
constexpr std::array<std::size_t, 2> MonarchCrestTexts{748, 1236};
constexpr std::uint16_t StatFruit = 0x1a0f;
constexpr std::array<std::size_t, 5> StatTexts{168, 169, 167, 166, 1900};
constexpr std::size_t AddStatText = 636;
constexpr std::size_t RemoveStatText = 1910;
constexpr std::array<std::size_t, 2> StatTails{AddStatText, RemoveStatText};
constexpr std::size_t RemovedText = 1908;
constexpr std::int32_t CommandFruitLevel = 4;
constexpr std::size_t UsableByText = 61;
constexpr std::size_t DarkLordText = 24;
constexpr std::uint16_t FirstCloakPiece = 0x1a10;
constexpr std::uint16_t SecondCloakPiece = 0x1a11;
constexpr std::size_t CloakText = 816;
constexpr std::uint16_t BloodCastleTicket = 0x1a12;
constexpr std::size_t BloodCastleText = 814;
constexpr std::size_t RightClickText = 638;
constexpr std::size_t EventTimeText = 639;
constexpr std::uint16_t SiegePotion = 0x1c07;
constexpr std::array<std::size_t, 3> BlessPotionTexts{1417, 1418, 1419};
constexpr std::array<std::size_t, 3> SoulPotionTexts{1638, 1639, 1472};
constexpr std::uint16_t Archer = 0x1a07;
constexpr std::array<std::size_t, 2> ArcherTexts{1460, 1461};
constexpr std::uint16_t GuardianScroll = 0x1a0b;
constexpr std::array<std::size_t, 2> GuardianScrollTexts{1416, 1462};
constexpr std::uint16_t BloodCastleInvitation = 0x1a1d;
constexpr std::uint16_t LordSign = 0x1c15;
constexpr std::int32_t LordSignLevel = 3;
constexpr std::size_t LordSignText = 1291;
constexpr std::uint16_t LostMap = 0x1c1c;
constexpr std::uint16_t MarkOfKundun = 0x1c1d;
constexpr std::size_t MarkCountText = 1181;
constexpr std::size_t MarkReadyText = 1182;
constexpr std::size_t MarkMissingText = 1183;
constexpr std::int32_t MarksForAMap = 5;

// 0x5b58ea..0x5b5a0c.
void WriteTheBundle(Frame& frame)
{
    const std::uint16_t type = frame.item.id;
    if (type != BlessBundle && type != SoulBundle)
        return;
    const auto count = static_cast<std::int8_t>(static_cast<std::uint8_t>((frame.level * 5 + 5) * 2));
    if (count <= 0)
        return;
    TooltipWriter& lines = frame.lines;
    lines.At("0x5b58fe");
    lines.Line(TooltipWhite, BundleText, static_cast<std::int32_t>(count),
               lines.Text(type == BlessBundle ? BlessText : SoulText));
    lines.Line(TooltipWhite, UnbundleText);
}

void WriteTheTypeText(Frame& frame)
{
    TooltipWriter& lines = frame.lines;
    lines.At("0x5b5a11");
    const std::uint16_t type = frame.item.id;
    for (const TypeText& entry : TypeTexts)
    {
        if (entry.type == type)
            lines.Line(TooltipWhite, entry.text);
    }
    if (type == Guardian)
    {
        lines.Line(TooltipWhite, AbsorbText, GuardianAbsorb);
        lines.Line(TooltipWhite, MaxLifeText, GuardianLife);
    }
    if (type == Imp)
        lines.Line(TooltipWhite, ImpText);
}

// 0x5b5c66..0x5b5e2a. false si no es un ala ni el 0x1a03.
bool WriteTheWings(Frame& frame, std::int32_t type)
{
    TooltipWriter& lines = frame.lines;
    const std::int32_t level = frame.level;
    if (InRange(type, FirstWing, LastWing))
    {
        const std::int32_t value = level * 2 + WingBase;
        lines.Line(TooltipWhite, AddDamageText, value);
        lines.Line(TooltipWhite, AbsorbText, value);
        lines.Line(TooltipWhite, SpeedText);
        return true;
    }
    if (InRange(type, FirstHighWing, LastHighWing))
    {
        lines.Line(TooltipWhite, AddDamageText, level * 2 + HighWingDamageBase);
        lines.Line(TooltipWhite, AbsorbText, level * 2 + HighWingAbsorbBase);
        lines.Line(TooltipWhite, SpeedText);
        return true;
    }
    if (type == Dinorant)
    {
        lines.Line(TooltipWhite, AddDamageText, DinorantDamage);
        lines.Line(TooltipWhite, AbsorbText, DinorantAbsorb);
        return true;
    }
    return false;
}

// 0x5b5f03..0x5b60c3: el 0x1a0f, sin negritas. Un nivel de más de 4 deja dos renglones vacíos.
void WriteTheStatFruit(Frame& frame)
{
    TooltipWriter& lines = frame.lines;
    const auto level = static_cast<std::size_t>(frame.level);
    for (const std::size_t tail : StatTails)
    {
        lines.Color(TooltipWhite);
        if (level < StatTexts.size())
            lines.Print("%s %s", lines.Text(StatTexts[level]), lines.Text(tail));
        lines.Next();
    }
    lines.Color(TooltipWhiteOnBlue);
    lines.PrintText(RemovedText);
    lines.Next();
    if (frame.level != CommandFruitLevel)
        return;
    lines.Color(TooltipWhite);
    lines.PrintText(UsableByText, lines.Text(DarkLordText));
    lines.Next();
}

// Un texto por nivel (0 y 1) en `color`: con otro nivel, nada.
void WriteALevelText(TooltipWriter& lines, std::int32_t level, const std::array<std::size_t, 2>& texts)
{
    if (level == 0 || level == 1)
        lines.PrintText(texts[static_cast<std::size_t>(level)]);
}

// 0x437c90 con el 0x1c1d: las marcas, sin color, y si alcanzan para el mapa.
void WriteTheMarks(Frame& frame)
{
    TooltipWriter& lines = frame.lines;
    lines.At("0x437cb0");
    const std::int32_t marks = frame.item.durability;
    lines.PrintText(MarkCountText, marks, MarksForAMap);
    lines.Next();
    if (marks >= MarksForAMap)
        lines.PrintText(MarkReadyText);
    else
        lines.PrintText(MarkMissingText, MarksForAMap - marks);
    lines.Color(TooltipGold);
    lines.Next();
}

// 0x5b5e34..0x5b6a2f: el resto de la cadena.
Core::Expected<void, std::string> WriteTheEventItem(Frame& frame, std::int32_t type)
{
    TooltipWriter& lines = frame.lines;
    const std::int32_t level = frame.level;
    if (type == PetSpirit)
        WriteALevelText(lines, level, PetSpiritTexts);
    else if (type == MonarchCrest)
    {
        lines.Color(TooltipWhite);
        if (level == 0 || level == 1)
        {
            lines.Print("%s", lines.Text(MonarchCrestTexts[static_cast<std::size_t>(level)]));
            lines.Next();
        }
    }
    else if (type == StatFruit)
        WriteTheStatFruit(frame);
    else if (type == FirstCloakPiece || type == SecondCloakPiece)
    {
        lines.Color(TooltipWhite);
        lines.PrintText(CloakText);
        lines.Next();
    }
    else if (type == BloodCastleTicket)
    {
        lines.Color(TooltipWhite);
        lines.PrintText(BloodCastleText);
        lines.Next();
        lines.NewLine();
        lines.Line(TooltipWhite, RightClickText);
        lines.Line(TooltipWhite, EventTimeText);
    }
    else if (type == SiegePotion)
    {
        if (level == 0 || level == 1)
        {
            for (const std::size_t text : level == 0 ? BlessPotionTexts : SoulPotionTexts)
                lines.Line(TooltipWhiteOnBlue, text);
        }
    }
    else if (type == Archer || type == GuardianScroll)
    {
        WriteALevelText(lines, level, type == Archer ? ArcherTexts : GuardianScrollTexts);
        lines.Color(TooltipGold);
        lines.Bold(0);
        lines.Next();
    }
    else if (type == BloodCastleInvitation)
        return Core::Unexpected(std::string("0x5b63b7: las líneas del 0x1a1d abren el registro propio y leen la tabla "
                                            "de niveles 0x6ba59c; no están portadas"));
    else if (type == LordSign)
    {
        lines.Color(TooltipWhite);
        if (level == LordSignLevel)
            lines.PrintText(LordSignText);
        lines.Next();
    }
    else if (type == LostMap)
        return Core::Unexpected(std::string("0x437d49: las líneas del 0x1c1c (0x437c90) abren el registro propio y "
                                            "leen la tabla de su clase; no están portadas"));
    else if (type == MarkOfKundun)
        WriteTheMarks(frame);
    return {};
}
} // namespace

Core::Expected<void, std::string> WriteTheNotes(ItemTooltipFrame& frame)
{
    WriteTheBundle(frame);
    WriteTheTypeText(frame);
    frame.lines.At("0x5b5c66");
    const std::int32_t type = static_cast<std::int16_t>(frame.item.id);
    if (WriteTheWings(frame, type))
        return {};
    return WriteTheEventItem(frame, type);
}
} // namespace Client::Scenes
