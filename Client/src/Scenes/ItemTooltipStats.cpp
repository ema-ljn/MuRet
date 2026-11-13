#include "Core/Expected.h"
#include "Scenes/ItemTooltipParts.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/TooltipWriter.h"
#include "World/ItemState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// 0x5b45f5..0x5b58e9 del cuerpo del ítem (06 § Los tooltips, "El cuerpo del ítem"): las líneas de algunos tipos, el
// daño y los stats. Los textos son los de Text.bmd (la dirección − 0x76bd1dc, sobre 300).
namespace Client::Scenes
{
namespace
{
using Frame = ItemTooltipFrame;

bool InRange(std::int32_t type, std::int32_t first, std::int32_t last)
{
    return type >= first && type <= last;
}

// 0x1a13 (0x5b45fb..0x5b4c07), el ítem de la misión del ángel: el 730 y el 815, el daño y los stats fijos de su nivel
// (0 a 2), el 87, el 94 y las cuatro opciones de su nivel (con el maná de la skill 0x16 o 0x18 en los niveles 1 y 2).
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
// El nivel 0: el 79 (0x35) en negrita, el 631 y el 632 (2). Los niveles 1 y 2: el maná de la skill 0x16 con el 84 o
// de la 0x18 con el 86, el 629 y el 630 (2). Después, el 628, el 633 (7), el 634 y el 635.
constexpr std::size_t MagicDamageText = 79;
constexpr std::int32_t AngelMagicDamage = 0x35;
constexpr std::size_t MagicLevelText = 631;
constexpr std::size_t MagicPercentText = 632;
constexpr std::size_t DamageLevelText = 629;
constexpr std::size_t DamagePercentText = 630;
constexpr std::int32_t AngelPercent = 2;
constexpr std::uint32_t CycloneSkill = 0x16;
constexpr std::size_t CycloneText = 84;
constexpr std::uint32_t TripleShotSkill = 0x18;
constexpr std::size_t TripleShotText = 86;
constexpr std::size_t OptimalText = 628;
constexpr std::size_t MagicSpeedText = 633;
constexpr std::int32_t AngelMagicSpeed = 7;
constexpr std::size_t LifeAfterKillText = 634;
constexpr std::size_t ManaAfterKillText = 635;

// 0x5b4c0d..0x5b4f8d: las misiones (0x1c17 a 0x1c1a), el 0x1c0c, el anillo 0x1a14 y el 0x1c13.
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

// 0x5b4f93..0x5b556b: el daño. 0xf0..0xff (los báculos y los libros) con el 42; las tres skills sin daño (0x1e05,
// 0x1e0e y 0x1e0f) sin renglón; los pergaminos de 0x1e00 a 0x1fff con el +0x21 de su entrada de Skill.bmd
// (0x54bf10); los demás con el 40 + Item.bmd+0x1e (una o dos manos).
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

// 0x5b4a91..0x5b4c07: las opciones del nivel del 0x1a13.
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

// Lo que no se puede guardar, comerciar ni tirar (0x5b4c58..0x5b4cdd y 0x5b4d82..0x5b4e07).
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
        // La tabla de 0x5b810c: el 926 en los niveles 0 y 3; en el 1 y el 2, el nivel desde el que se tira y los tres.
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

// 0x54bf10: la skill de un pergamino, el tipo − 0x1dff, salvo 0x1e10, 0x1e11 y 0x1e12, que son 0x26, 0x27 y 0x28.
std::int32_t ScrollSkillOf(std::int32_t type)
{
    constexpr std::int32_t FirstRenumbered = 0x1e10;
    constexpr std::array<std::int32_t, 3> Renumbered{0x26, 0x27, 0x28};
    const std::int32_t index = type - FirstRenumbered;
    if (index >= 0 && index < static_cast<std::int32_t>(Renumbered.size()))
        return Renumbered[static_cast<std::size_t>(index)];
    return type - ScrollSkillBase;
}

// 0x5b4f93..0x5b556b. El renglón queda escrito; con el mínimo en 0 o en las skills sin daño, el que sigue baja uno.
Core::Expected<void, std::string> WriteTheDamage(Frame& frame)
{
    TooltipWriter& lines = frame.lines;
    lines.At("0x5b4f93");
    const World::ItemState& item = frame.item;
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
// 0x5b556c..0x5b5770: la defensa (azul en las armaduras excelentes), la resistencia, la tasa de defensa (azul con
// alguna excelente), las velocidades de Item.bmd y el 2011 de las pociones de asedio.
void WriteTheDefenses(Frame& frame)
{
    TooltipWriter& lines = frame.lines;
    lines.At("0x5b556c");
    const World::ItemState& item = frame.item;
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

// 0x5b5771..0x5b58e4: el 0x1c0b, con sus textos del nivel 7 o 14 (o el 571), el 733 y, en el 13, el 731.
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
} // namespace Client::Scenes
