#include "Core/Expected.h"
#include "Models/LogicalAccess.h"
#include "Hud/GameHud.h"
#include "Tooltips/ItemTooltipParts.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Tooltips/TooltipLines.h"
#include "Tooltips/TooltipWriter.h"
#include "Kernel/WorldState.h"
#include "Kernel/ItemState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// 0x5b58ea..0x5b6a3b of the item body (06 § Los tooltips, "El cuerpo del ítem"): the jewel bundles, the text of each
// jewel and of each event object, the wings and the capes. The texts are the Text.bmd ones (the address − 0x76bd1dc,
// over 300).
namespace Client::Tooltips
{
namespace
{
using Frame = ItemTooltipFrame;

bool InRange(std::int32_t type, std::int32_t first, std::int32_t last)
{
    return type >= first && type <= last;
}

// 0x402b30 and 0x402d30: the Bless (2) and Soul (3) bundles count (5 · level + 5) · 2 jewels, in a signed byte: from
// level 12 on it is negative and there is no line.
constexpr std::uint16_t BlessBundle = 0x181e;
constexpr std::uint16_t SoulBundle = 0x181f;
constexpr std::size_t BundleText = 1819;
constexpr std::size_t UnbundleText = 1820;
constexpr std::size_t BlessText = 1806;
constexpr std::size_t SoulText = 1807;

// 0x5b5a11..0x5b5c60: one loose text per type, in white.
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

// 0x5b5c66..0x5b5e2a: the wings and 0x1a03 with the damage they add, the one they absorb and 579.
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
// 0x5b63b7..0x5b69e7: the Chaos Castle armor and its levels.
constexpr std::uint16_t ChaosCastleArmor = 0x1a1d;
// 0x5b63c0..0x5b63d0: classes 3 and 4 (+0x1d4 & 7, without the evolution) read from the seventh row of 0x6ba598.
constexpr std::uint8_t HeroClassBits = 7;
constexpr std::uint8_t FirstClassOfTheSecondTable = 3;
constexpr std::uint8_t SecondClassOfTheSecondTable = 4;
constexpr std::size_t ChaosCastleCount = 6;
struct LevelRange
{
    std::int32_t minimum = 0;
    std::int32_t maximum = 0;
};
// 0x6ba598, signed dwords (0x5b68cd points at the maximum, 0x6ba59c, and reads the minimum at −4): each castle's
// minimum and maximum level, six rows for the other classes and six for 3 and 4. In build 2f16a9c6 the last two
// maximums are 999.
constexpr std::array<LevelRange, 2 * ChaosCastleCount> ChaosCastleLevels{{{15, 49},
                                                                          {50, 119},
                                                                          {120, 179},
                                                                          {180, 239},
                                                                          {240, 299},
                                                                          {300, 400},
                                                                          {15, 29},
                                                                          {30, 99},
                                                                          {100, 159},
                                                                          {160, 219},
                                                                          {220, 279},
                                                                          {280, 400}}};
// 0x6ba5f8..0x6ba610: each castle's entry, in thousands of zen; the same for both tables.
constexpr std::array<std::int32_t, ChaosCastleCount> ChaosCastleThousands{25, 80, 150, 250, 400, 650};
// 0x5b68dd: the written maximum, signed (JLE); the comparison with the level uses the table's.
constexpr std::int32_t ShownMaximumLevel = 400;
// 0x6c9b80 and 0x6c9b98, from the .data.
constexpr std::string_view ChaosCastleHeaderFormat = "%s %s    %s      %s    ";
constexpr std::string_view ChaosCastleRowFormat = "        %d             %3d~%3d     %3d,000";
constexpr std::size_t ChaosCastleText = 1147;       // 0x7711200
constexpr std::size_t LevelText = 368;              // 0x76d811c
constexpr std::size_t MinimumLevelText = 935;       // 0x7701990
constexpr std::size_t PriceText = 936;              // 0x7701abc
constexpr std::size_t RightClickToEnterText = 1157; // 0x7711db8
constexpr std::uint16_t LordSign = 0x1c15;
constexpr std::int32_t LordSignLevel = 3;
constexpr std::size_t LordSignText = 1291;
constexpr std::uint16_t LostMap = 0x1c1c;
// 0x437d14..0x437d26: classes 3 and 4 (+0x1d4 & 7) read from the eighth row of 0x6b8b18 ([EBP − 4] at 7).
constexpr std::size_t KalimaCount = 7;
// 0x6b8b18 (.rdata), signed dwords: each Kalima's minimum and maximum level, seven rows for the other classes and
// seven for 3 and 4. 0x4380c1 points at the maximum (0x6b8b1c) and reads the minimum at −4.
constexpr std::array<LevelRange, 2 * KalimaCount> KalimaLevels{{{40, 999},
                                                                {131, 999},
                                                                {181, 999},
                                                                {231, 999},
                                                                {281, 999},
                                                                {331, 999},
                                                                {350, 999},
                                                                {20, 999},
                                                                {111, 999},
                                                                {161, 999},
                                                                {211, 999},
                                                                {261, 999},
                                                                {311, 999},
                                                                {350, 999}}};
// 0x6c7468 and 0x6c7444, from the .data.
constexpr std::string_view KalimaHeaderFormat = "%s %s       %s    ";
constexpr std::string_view KalimaRowFormat = "        %d             %3d~%3d     ";
// 0x6c6484.
constexpr std::string_view KalimaBreak = "\n";
constexpr std::size_t KalimaText = 58;            // 0x76c15d4
constexpr std::size_t MagicStoneText = 1184;      // 0x7713d5c
constexpr std::size_t KalimaFromLevelText = 1123; // 0x770f5e0
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

// 0x5b5c66..0x5b5e2a. false if it is not a wing or 0x1a03.
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

// 0x5b5f03..0x5b60c3: 0x1a0f, without bold. A level above 4 leaves two empty lines.
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

// One text per level (0 and 1) in `color`: with another level, nothing.
void WriteALevelText(TooltipWriter& lines, std::int32_t level, const std::array<std::size_t, 2>& texts)
{
    if (level == 0 || level == 1)
        lines.PrintText(texts[static_cast<std::size_t>(level)]);
}

// 0x437c90 with 0x1c1d: the marks, without color, and whether they are enough for the map.
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

// 0x437d49..0x4381e0: 0x1c1c (the Lost Map) in 0x437c90, which 0x5b6a2f calls with the line and which returns the next
// one (0x5b6a37 keeps it in 0x78abb30 and does not touch 0x78abb34: its "\n" are not half lines). The hero's class
// (+0x1d4 & 7) picks the table; it opens the own record (inline, like 0x40b240), reads its level (the word +0xe of
// [0x7681eb0]) and closes it: 3848 rand() if its count returns to 0. Then, the header in color 0 and without bold; the
// seven rows with the maximum written up to 400 (JLE) and the map level's one ((D+4 >> 3) & 0xf) in color 8, without
// bold; a "\n" without color or bold; 1184 in color 7, without writing the bold; and, with the own level below the
// first row's minimum (JGE, signed), another "\n" and 1123 with that minimum in color 5.
void WriteTheKalimaLevels(Frame& frame)
{
    TooltipWriter& lines = frame.lines;
    lines.At("0x437d49");
    if (lines.Failed())
        return;
    Hud::GameHudContext& context = frame.context;
    const std::uint8_t heroClass = HeroClassOf(context) & HeroClassBits;
    const std::size_t first =
        heroClass == FirstClassOfTheSecondTable || heroClass == SecondClassOfTheSecondTable ? KalimaCount : 0;
    std::int32_t ownLevel = 0;
    {
        const Models::LogicalAccess own(context.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        ownLevel = context.world.OwnCharacter().level;
    }
    lines.At("0x438071");
    lines.Color(TooltipWhite);
    lines.Print(KalimaHeaderFormat, lines.Text(KalimaText), lines.Text(LevelText), lines.Text(MinimumLevelText));
    lines.Color(TooltipWhite);
    lines.Bold(0);
    lines.Next();
    for (std::size_t i = 0; i < KalimaCount; ++i)
    {
        const LevelRange& range = KalimaLevels[first + i];
        const auto row = static_cast<std::int32_t>(i + 1);
        lines.Print(KalimaRowFormat, row, range.minimum, std::min(range.maximum, ShownMaximumLevel));
        lines.Color(frame.level == row ? TooltipWhiteOnTeal : TooltipWhite);
        lines.Bold(0);
        lines.Next();
    }
    lines.At("0x43813a");
    lines.Print(KalimaBreak);
    lines.Next();
    lines.PrintText(MagicStoneText);
    lines.Color(TooltipWhiteOnRed);
    lines.Next();
    const std::int32_t minimum = KalimaLevels[first].minimum;
    if (ownLevel >= minimum)
        return;
    lines.At("0x438193");
    lines.Print(KalimaBreak);
    lines.Next();
    lines.PrintText(KalimaFromLevelText, minimum);
    lines.Color(TooltipWhiteOnBlue);
    lines.Next();
}

// 0x5b63b7..0x5b69e7: 0x1a1d. Before writing, it opens the own record (inline, like 0x40b240 and 0x40b4d0), reads its
// level (the word +0xe of [0x7681eb0]) and closes it: 3848 rand() if its count returns to 0. Then, the header in
// white; one row per castle from the table of the hero's class, with the maximum written up to 400 and the one of its
// level in color 8 (signed and with both edges, against the table's maximum), without bold; a "\n" without color or
// bold; and 1157 in color 7, without writing the bold.
void WriteTheChaosCastleLevels(Frame& frame)
{
    TooltipWriter& lines = frame.lines;
    lines.At("0x5b63b7");
    if (lines.Failed())
        return;
    Hud::GameHudContext& context = frame.context;
    const std::uint8_t heroClass = HeroClassOf(context) & HeroClassBits;
    const std::size_t first =
        heroClass == FirstClassOfTheSecondTable || heroClass == SecondClassOfTheSecondTable ? ChaosCastleCount : 0;
    std::int32_t level = 0;
    {
        const Models::LogicalAccess own(context.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        level = context.world.OwnCharacter().level;
    }
    lines.At("0x5b6874");
    lines.Print(ChaosCastleHeaderFormat, lines.Text(ChaosCastleText), lines.Text(LevelText),
                lines.Text(MinimumLevelText), lines.Text(PriceText));
    lines.Color(TooltipWhite);
    lines.Bold(0);
    lines.Next();
    for (std::size_t i = 0; i < ChaosCastleCount; ++i)
    {
        const LevelRange& range = ChaosCastleLevels[first + i];
        lines.Print(ChaosCastleRowFormat, static_cast<std::int32_t>(i + 1), range.minimum,
                    std::min(range.maximum, ShownMaximumLevel), ChaosCastleThousands[i]);
        const bool heroRow = level >= range.minimum && level <= range.maximum;
        lines.Color(heroRow ? TooltipWhiteOnTeal : TooltipWhite);
        lines.Bold(0);
        lines.Next();
    }
    lines.At("0x5b697b");
    lines.NewLine();
    lines.PrintText(RightClickToEnterText);
    lines.Color(TooltipWhiteOnRed);
    lines.Next();
}

// 0x5b5e34..0x5b6a2f: the rest of the chain.
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
    else if (type == ChaosCastleArmor)
        WriteTheChaosCastleLevels(frame);
    else if (type == LordSign)
    {
        lines.Color(TooltipWhite);
        if (level == LordSignLevel)
            lines.PrintText(LordSignText);
        lines.Next();
    }
    else if (type == LostMap)
        WriteTheKalimaLevels(frame);
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
} // namespace Client::Tooltips
