#include "Core/Expected.h"
#include "Tooltips/ItemTooltipParts.h"
#include "Tooltips/TooltipLines.h"
#include "Tooltips/TooltipWriter.h"
#include "Kernel/ItemState.h"
#include "Tables/SkillId.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// 0x5b45f5..0x5b58e9 of the item body (06 § Los tooltips, "El cuerpo del ítem"): some types' lines, the damage and
// the stats. The texts are the Text.bmd ones (the address − 0x76bd1dc, over 300).
namespace Client::Tooltips
{
namespace
{
using Frame = ItemTooltipFrame;

bool InRange(std::int32_t type, std::int32_t first, std::int32_t last)
{
    return type >= first && type <= last;
}

// 0x1a13 (0x5b45fb..0x5b4c07), the angel quest's item: 730 and 815, the damage and the fixed stats of its level (0 to
// 2), 87, 94 and the four options of its level (with the mana of skill 0x16 or 0x18 at levels 1 and 2).
constexpr std::uint16_t AngelQuestItem = 0x1a13;
constexpr std::size_t QuestText = 730;
constexpr std::size_t AngelText = 815;
constexpr std::size_t DamageFormatText = 42;
constexpr std::size_t OneHandedText = 40;
constexpr std::size_t TwoHandedText = 41;
constexpr std::size_t AttackSpeedText = 64;
constexpr std::size_t StrengthText = 73;
constexpr std::size_t AgilityText = 75;
constexpr std::size_t LuckText = 87;
constexpr std::size_t CriticalText = 94;
constexpr std::int32_t AngelCritical = 0x14;

struct AngelLevel
{
    std::size_t damageText = 0;
    std::int32_t minimum = 0;
    std::int32_t maximum = 0;
    std::int32_t speed = 0;
    std::int32_t strength = 0;
    std::int32_t agility = 0;
};

constexpr std::array<AngelLevel, 3> AngelLevels{{{DamageFormatText, 0x6b, 0x6e, 0x14, 0x84, 0x20},
                                                 {OneHandedText, 0x6e, 0x78, 0x23, 0x17d, 0x95},
                                                 {TwoHandedText, 0x78, 0x8c, 0x23, 0x8c, 0x15e}}};
// Level 0: 79 (0x35) in bold, 631 and 632 (2). Levels 1 and 2: the mana of skill 0x16 with 84 or of 0x18 with 86, 629
// and 630 (2). Then, 628, 633 (7), 634 and 635.
constexpr std::size_t MagicDamageText = 79;
constexpr std::int32_t AngelMagicDamage = 0x35;
constexpr std::size_t MagicLevelText = 631;
constexpr std::size_t MagicPercentText = 632;
constexpr std::size_t DamageLevelText = 629;
constexpr std::size_t DamagePercentText = 630;
constexpr std::int32_t AngelPercent = 2;
constexpr std::uint32_t CycloneSkill = 0x16;
constexpr std::size_t CycloneText = 84;
constexpr std::uint32_t TripleShotSkill = Tables::TripleShotSkill;
constexpr std::size_t TripleShotText = 86;
constexpr std::size_t OptimalText = 628;
constexpr std::size_t MagicSpeedText = 633;
constexpr std::int32_t AngelMagicSpeed = 7;
constexpr std::size_t LifeAfterKillText = 634;
constexpr std::size_t ManaAfterKillText = 635;

// 0x5b4c0d..0x5b4f8d: the quests (0x1c17 to 0x1c1a), 0x1c0c, the ring 0x1a14 and 0x1c13.
constexpr std::int32_t FirstQuest = 0x1c17;
constexpr std::int32_t LastQuest = 0x1c1a;
constexpr std::size_t NoWarehouseText = 731;
constexpr std::size_t NoTradeText = 732;
constexpr std::size_t NoDropText = 733;
constexpr std::uint16_t EventTicket = 0x1c0c;
constexpr std::int32_t EventTicketLastLevel = 1;
constexpr std::size_t RegisterText = 119;
constexpr std::uint16_t WarriorRing = 0x1a14;
constexpr std::size_t DropLevelText = 924;
constexpr std::size_t NoRepairText = 926;
constexpr std::array<std::int32_t, 2> WarriorRingDropLevels{0x28, 0x50};
constexpr std::uint16_t EventInvitation = 0x1c13;
constexpr std::size_t RightClickText = 638;
constexpr std::size_t EventTimeText = 639;

// 0x5b4f93..0x5b556b: the damage. 0xf0..0xff (the staffs and the books) with 42; the three skills without damage
// (0x1e05, 0x1e0e and 0x1e0f) without a line; the scrolls from 0x1e00 to 0x1fff with the +0x21 of their Skill.bmd
// entry (0x54bf10); the rest with 40 + Item.bmd+0x1e (one or two hands).
constexpr std::int32_t StaffMask = 0xfff0;
constexpr std::int32_t Staffs = 0xf0;
constexpr std::array<std::int32_t, 3> SkillsWithoutDamage{0x1e05, 0x1e0e, 0x1e0f};
constexpr std::int32_t FirstScroll = 0x1e00;
constexpr std::int32_t ScrollsEnd = 0x2000;
constexpr std::int32_t ScrollSkillBase = 0x1dff;
constexpr std::size_t TwoHandedByte = 0x1e;
constexpr char DamageFormat[] = "%s: %d ~ %d";

// 0x5b556c..0x5b58e9.
constexpr std::size_t DefenseText = 65;
constexpr std::int32_t FirstArmor = 0xe00;
constexpr std::int32_t ArmorsEnd = 0x1800;
constexpr std::size_t SpellResistanceText = 66;
constexpr std::size_t DefenseRateByte = 0x28;
constexpr std::size_t DefenseRateText = 67;
constexpr std::size_t AttackSpeedByte = 0x2b;
constexpr std::size_t WalkSpeedByte = 0x2c;
constexpr std::size_t WalkSpeedText = 68;
constexpr std::int32_t FirstSiegePotion = 0x1c20;
constexpr std::int32_t LastSiegePotion = 0x1c22;
constexpr std::size_t SiegePotionText = 2011;
constexpr std::array<std::int32_t, 3> SiegePotionColors{TooltipPurple, TooltipRed, TooltipBlue};
constexpr std::uint16_t LuckyBox = 0x1c0b;
constexpr std::int32_t HeavenBoxLevel = 7;
constexpr std::array<std::size_t, 3> HeavenBoxTexts{112, 113, 114};
constexpr std::int32_t KalimaTicketLevel = 14;
constexpr std::array<std::size_t, 2> KalimaTicketTexts{1652, 1653};
constexpr std::size_t ChaosText = 571;
constexpr std::int32_t DarkLordHeartLevel = 13;

// 0x5b4a91..0x5b4c07: the options of 0x1a13's level.
Core::Expected<void, std::string> WriteTheAngelOptions(Frame& frame)
{
    TooltipWriter& lines = frame.lines;
    if (frame.level == 0)
    {
        lines.PrintText(MagicDamageText, AngelMagicDamage);
        lines.Color(TooltipBlue);
        lines.Bold(1);
        lines.Next();
        lines.Line(TooltipBlue, MagicLevelText);
        lines.Line(TooltipBlue, MagicPercentText, AngelPercent);
    }
    else
    {
        const bool cyclone = frame.level == 1;
        const auto entry =
            ReadTheSkillEntry(frame, cyclone ? CycloneSkill : TripleShotSkill, cyclone ? "0x5b49dd" : "0x5b491a");
        if (!entry.HasValue())
            return Core::Unexpected(entry.Error());
        lines.Line(TooltipBlue, cyclone ? CycloneText : TripleShotText, static_cast<std::int32_t>(entry.Value().mana));
        lines.Line(TooltipBlue, DamageLevelText);
        lines.Line(TooltipBlue, DamagePercentText, AngelPercent);
    }
    lines.Line(TooltipBlue, OptimalText);
    lines.Line(TooltipBlue, MagicSpeedText, AngelMagicSpeed);
    lines.Line(TooltipBlue, LifeAfterKillText);
    lines.Line(TooltipBlue, ManaAfterKillText);
    return {};
}

Core::Expected<void, std::string> WriteTheAngelQuestItem(Frame& frame)
{
    TooltipWriter& lines = frame.lines;
    lines.At("0x5b45fb");
    lines.Line(TooltipWhite, QuestText);
    lines.Line(TooltipWhiteOnBlue, AngelText);
    lines.Print("\n");
    lines.Bold(0);
    lines.Next();
    lines.AddHalf();
    if (frame.level >= static_cast<std::int32_t>(AngelLevels.size()))
        return Core::Unexpected(std::string("0x5b47a6: el 0x1a13 con nivel 3 o más escribe la dirección del ítem como "
                                            "velocidad y requisitos"));
    const AngelLevel& stats = AngelLevels[static_cast<std::size_t>(frame.level)];
    lines.Print(DamageFormat, lines.Text(stats.damageText), stats.minimum, stats.maximum);
    lines.Color(TooltipWhite);
    lines.Bold(0);
    lines.Next();
    lines.Line(TooltipWhite, AttackSpeedText, stats.speed);
    lines.Line(TooltipWhite, StrengthText, stats.strength);
    lines.Line(TooltipWhite, AgilityText, stats.agility);
    lines.Print("\n");
    lines.Bold(0);
    lines.Next();
    lines.AddHalf();
    lines.Line(TooltipBlue, LuckText);
    lines.Line(TooltipBlue, CriticalText, AngelCritical);
    return WriteTheAngelOptions(frame);
}

// What cannot be stored, traded or dropped (0x5b4c58..0x5b4cdd and 0x5b4d82..0x5b4e07).
void WriteTheLocks(TooltipWriter& lines)
{
    lines.Line(TooltipRed, NoWarehouseText);
    lines.Line(TooltipRed, NoTradeText);
    lines.Line(TooltipRed, NoDropText);
}

void WriteTheOtherKinds(Frame& frame)
{
    TooltipWriter& lines = frame.lines;
    lines.At("0x5b4c0d");
    const std::int32_t type = static_cast<std::int16_t>(frame.item.id);
    if (InRange(type, FirstQuest, LastQuest))
    {
        lines.Line(TooltipWhite, QuestText);
        WriteTheLocks(lines);
    }
    else if (type == EventTicket)
    {
        if (frame.level <= EventTicketLastLevel)
            lines.Line(TooltipWhite, RegisterText);
    }
    else if (type == WarriorRing)
    {
        // The table at 0x5b810c: 926 at levels 0 and 3; at 1 and 2, the level from which it is dropped and the three.
        if (frame.level == 0 || frame.level == 3)
            lines.Line(TooltipRed, NoRepairText);
        else if (frame.level == 1 || frame.level == 2)
        {
            lines.Line(TooltipWhite, DropLevelText, WarriorRingDropLevels[static_cast<std::size_t>(frame.level - 1)]);
            WriteTheLocks(lines);
        }
    }
    if (type == EventInvitation)
    {
        lines.Line(TooltipWhite, RightClickText);
        lines.Line(TooltipWhite, EventTimeText);
    }
}

// 0x54bf10: a scroll's skill, the type − 0x1dff, except 0x1e10, 0x1e11 and 0x1e12, which are 0x26, 0x27 and 0x28.
std::int32_t ScrollSkillOf(std::int32_t type)
{
    constexpr std::int32_t FirstRenumbered = 0x1e10;
    constexpr std::array<std::int32_t, 3> Renumbered{0x26, 0x27, 0x28};
    const std::int32_t index = type - FirstRenumbered;
    if (index >= 0 && index < static_cast<std::int32_t>(Renumbered.size()))
        return Renumbered[static_cast<std::size_t>(index)];
    return type - ScrollSkillBase;
}

// 0x5b4f93..0x5b556b. The line stays written; with the minimum at 0 or in the skills without damage, the next one goes
// down one.
Core::Expected<void, std::string> WriteTheDamage(Frame& frame)
{
    TooltipWriter& lines = frame.lines;
    lines.At("0x5b4f93");
    const Kernel::ItemState& item = frame.item;
    if (item.word0c == 0)
        return {};
    const std::int32_t type = static_cast<std::int16_t>(item.id);
    std::int32_t minimum = item.word0c;
    if ((type & StaffMask) == Staffs)
        lines.Print(DamageFormat, lines.Text(DamageFormatText), minimum, static_cast<std::int32_t>(item.word0e));
    else if (std::ranges::find(SkillsWithoutDamage, type) != SkillsWithoutDamage.end())
        lines.Back();
    else if (type >= FirstScroll && type < ScrollsEnd)
    {
        const auto skill = static_cast<std::uint32_t>(ScrollSkillOf(type)) & 0xffu;
        const auto entry = ReadTheSkillEntry(frame, skill, "0x5b502f");
        if (!entry.HasValue())
            return Core::Unexpected(entry.Error());
        minimum = entry.Value().byte21;
        const std::int32_t maximum = minimum + (minimum >> 1);
        lines.Print(DamageFormat, lines.Text(DamageFormatText), minimum, maximum);
    }
    else
    {
        lines.Print(DamageFormat, lines.Text(OneHandedText + RecordByte(frame, TwoHandedByte)), minimum,
                    static_cast<std::int32_t>(item.word0e));
    }
    // 0x5b5533..0x5b5566.
    if (minimum > 0)
    {
        lines.Color(IsExcellent(item) ? TooltipBlue : TooltipWhite);
        lines.Bold(0);
        lines.Next();
    }
    else
        lines.Back();
    return {};
}
// 0x5b556c..0x5b5770: the defense (blue in the excellent armors), the resistance, the defense rate (blue with some
// excellent option), Item.bmd's speeds and 2011 of the siege potions.
void WriteTheDefenses(Frame& frame)
{
    TooltipWriter& lines = frame.lines;
    lines.At("0x5b556c");
    const Kernel::ItemState& item = frame.item;
    const std::int32_t type = static_cast<std::int16_t>(item.id);
    if (item.word12 != 0)
    {
        lines.PrintText(DefenseText, static_cast<std::int32_t>(item.word12));
        const bool armor = type >= FirstArmor && type < ArmorsEnd;
        lines.Color(armor && IsExcellent(item) ? TooltipBlue : TooltipWhite);
        lines.Bold(0);
        lines.Next();
    }
    if (item.word14 != 0)
        lines.Line(TooltipWhite, SpellResistanceText, static_cast<std::int32_t>(item.word14));
    if (RecordByte(frame, DefenseRateByte) != 0)
    {
        lines.PrintText(DefenseRateText, static_cast<std::int32_t>(item.byte10));
        lines.Color(IsExcellent(item) ? TooltipBlue : TooltipWhite);
        lines.Bold(0);
        lines.Next();
    }
    if (const std::uint8_t speed = RecordByte(frame, AttackSpeedByte); speed != 0)
        lines.Line(TooltipWhite, AttackSpeedText, static_cast<std::int32_t>(speed));
    if (const std::uint8_t walk = RecordByte(frame, WalkSpeedByte); walk != 0)
        lines.Line(TooltipWhite, WalkSpeedText, static_cast<std::int32_t>(walk));
    if (InRange(type, FirstSiegePotion, LastSiegePotion))
    {
        lines.PrintText(SiegePotionText);
        lines.Color(SiegePotionColors[static_cast<std::size_t>(type - FirstSiegePotion)]);
        lines.Bold(0);
        lines.Next();
    }
}

// 0x5b5771..0x5b58e4: 0x1c0b, with its texts of level 7 or 14 (or 571), 733 and, at 13, 731.
void WriteTheLuckyBox(Frame& frame)
{
    TooltipWriter& lines = frame.lines;
    if (frame.item.id != LuckyBox)
        return;
    lines.At("0x5b5771");
    if (frame.level == HeavenBoxLevel)
    {
        for (const std::size_t text : HeavenBoxTexts)
            lines.Line(TooltipWhite, text);
    }
    else if (frame.level == KalimaTicketLevel)
    {
        for (const std::size_t text : KalimaTicketTexts)
            lines.Line(TooltipWhite, text);
    }
    else
        lines.Line(TooltipWhite, ChaosText);
    lines.Line(TooltipRed, NoDropText);
    if (frame.level == DarkLordHeartLevel)
        lines.Line(TooltipRed, NoWarehouseText);
}
} // namespace

Core::Expected<void, std::string> WriteTheKindLines(ItemTooltipFrame& frame)
{
    if (frame.item.id == AngelQuestItem)
    {
        if (auto written = WriteTheAngelQuestItem(frame); !written.HasValue())
            return written;
    }
    WriteTheOtherKinds(frame);
    return {};
}

Core::Expected<void, std::string> WriteTheStats(ItemTooltipFrame& frame)
{
    if (auto damage = WriteTheDamage(frame); !damage.HasValue())
        return damage;
    WriteTheDefenses(frame);
    WriteTheLuckyBox(frame);
    return {};
}
} // namespace Client::Tooltips
