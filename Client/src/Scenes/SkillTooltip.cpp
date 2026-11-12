#include "Scenes/SkillTooltip.h"

#include "Core/Expected.h"
#include "Data/Tables/SkillTable.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/ScreenScale.h"
#include "Render/SpriteBatch.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameTooltips.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/ItemSetOptionList.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/ItemTooltipParts.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnDamage.h"
#include "Scenes/OwnStatHelpers.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/TooltipWriter.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ItemState.h"
#include "World/OriginalFtol.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
// +0x1d4 del héroe & 7: la clase.
constexpr std::uint8_t ClassBits = 7;
constexpr std::uint32_t WizardClass = 0;
constexpr std::uint32_t KnightClass = 1;
constexpr std::uint32_t ElfClass = 2;
constexpr std::uint32_t GladiatorClass = 3;
constexpr std::uint32_t DarkLordClass = 4;

// 0x421c1a..0x421c3d: los lugares de los cuatro comandos del cuervo, con el Dark Lord.
constexpr std::int32_t FirstCommand = 0x78;
constexpr std::int32_t CommandCount = 4;
// 0x421c60 y 0x421cb5..0x421cca: el nombre (1219 a 1222) y la ayuda (1223 a 1226) de cada comando.
constexpr std::size_t CommandNameText = 1219;
constexpr std::size_t CommandHelpText = 1223;
// 0x421cd9..0x421ce1: ESI (los renglones) y EBX (los medios) en 4.
constexpr std::int32_t CommandLines = 4;
constexpr std::int32_t CommandHalves = 4;
// 0x5ba6df..0x5ba6ed: los tres "\n" de la skill (los renglones 0 y 2 y el último) son medios renglones.
constexpr std::int32_t SkillHalves = 3;
// 0x5af090: sin ancho, el modo 2 y el marco.
constexpr std::int32_t CenteredMode = 2;
constexpr std::int32_t WithFrame = 1;

// 0x6c9c38, 0x6c9c40, 0x6c9c44 y 0x6c6484: "\n"; 0x6c9c34 y 0x6c9c3c: "%s".
constexpr std::string_view NewLineFormat = "\n";
constexpr std::string_view TextFormat = "%s";

// Las skills que el tooltip mira, por lo que hace con ellas.
// 0x5b93ec: la del Wizard con la absorción (578, 880 y 881).
constexpr std::uint8_t AbsorbSkill = 0x10;
// 0x5b9810..0x5b9819: las de la elfa con su renglón de la energía (171, 172 y 173).
constexpr std::uint8_t HealSkill = 0x1a;
constexpr std::uint8_t DefenseSkill = 0x1b;
constexpr std::uint8_t AttackSkill = 0x1c;
// 0x5b9996: la del Dark Knight con el texto 96.
constexpr std::uint8_t NotMountedSkill = 0x2f;
// 0x5b9239: la que cambia de nombre con la opción 0x42 en la derecha.
constexpr std::uint8_t ForceSkill = 0x3c;
// 0x5ba73c, el caso 1: la de las clases 1 a 4 con el texto 1237 en vez del daño.
constexpr std::uint8_t PetSkill = 0x3e;
// 0x5ba3e0: la del Dark Lord con el texto 1185 sin party.
constexpr std::uint8_t PartySkill = 0x3f;
// 0x5b958d y 0x5ba42a: la del daño de los stats (0x552470) y las dos notas.
constexpr std::uint8_t StatDamageSkill = 0x4c;
// 0x5b94cf..0x5b94e1: con el cuarto argumento en una de estas, el Wizard no escribe la magia.
constexpr std::int32_t FirstSiegeSkill = 0x43;
constexpr std::int32_t LastSiegeSkill = 0x45;
// 0x5ba73c, el caso 0: las de las clases 1 a 4 sin renglón de daño (de 6 a 0x4c; fuera de ese rango, el caso 2).
constexpr std::array<std::uint8_t, 26> NoDamageSkills{0x06, 0x0f, 0x10, 0x12, 0x1a, 0x1b, 0x1c, 0x1e, 0x1f,
                                                      0x20, 0x21, 0x22, 0x23, 0x24, 0x30, 0x35, 0x3f, 0x40,
                                                      0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x4b, 0x4c};
// 0x5b99e7..0x5b99fd: las del combo del Dark Knight, de nivel 220 o más.
constexpr std::uint8_t FirstComboSkill = 0x13;
constexpr std::uint8_t LastComboSkill = 0x17;
constexpr std::array<std::uint8_t, 3> OtherComboSkills{0x29, 0x2a, 0x2b};
constexpr std::uint16_t ComboLevel = 220;
// 0x5b99d1: el +0x390 del héroe en 1.
constexpr std::uint8_t ComboQuestDone = 1;

// 0x5b9249: la derecha sin ítem; 0x5b926f: la opción de la onda.
constexpr std::uint16_t NoModel = 0xffff;
constexpr std::uint8_t ForceWaveOption = 0x42;
// [EBP−0x148]: el buffer del nombre, de 256 bytes.
constexpr std::size_t NameBufferBytes = 0x100;

// Los textos, (dirección − 0x76bd1dc) / 300.
constexpr std::size_t MagicDamageText = 170;     // 0x76c9914
constexpr std::size_t HealText = 171;            // 0x76c9a40
constexpr std::size_t DefenseText = 172;         // 0x76c9b6c
constexpr std::size_t AttackText = 173;          // 0x76c9c98
constexpr std::size_t RangeText = 174;           // 0x76c9dc4
constexpr std::size_t ManaText = 175;            // 0x76c9ef0
constexpr std::size_t NotMountedText = 96;       // 0x76c425c
constexpr std::size_t ComboText = 99;            // 0x76c45e0
constexpr std::size_t StaminaText = 360;         // 0x76d77bc
constexpr std::size_t AbsorbText = 578;          // 0x76e7734
constexpr std::size_t SkillDamageText = 879;     // 0x76fd7f0
constexpr std::size_t AbsorbManaText = 880;      // 0x76fd91c
constexpr std::size_t AbsorbTimeText = 881;      // 0x76fda48
constexpr std::size_t FirstAttributeText = 1080; // 0x770c37c
constexpr std::size_t PartyText = 1185;          // 0x7713e88
constexpr std::size_t ForceWaveText = 1200;      // 0x771501c
constexpr std::size_t PetText = 1237;            // 0x7717b78
constexpr std::size_t GiverText = 1480;          // 0x772983c
constexpr std::size_t GiverDelayText = 1481;     // 0x7729968
constexpr std::size_t GuildWarText = 1482;       // 0x7729a94
constexpr std::size_t KillCountText = 1483;      // 0x7729bc0
constexpr std::size_t FirstStatNoteText = 1926;  // 0x774a2e4
constexpr std::size_t SecondStatNoteText = 1927; // 0x774a410

// Skill.bmd: el byte +0x21 (el daño), el +0x30 (el atributo, 0xff sin él), el +0x31 (de dónde viene: 2, de otra
// skill, la del +0x32; 1, de la guerra de guilds, con el +0x33).
constexpr std::size_t DamageByte = 0x21;
constexpr std::size_t AttributeByte = 0x30;
constexpr std::uint8_t NoAttribute = 0xff;
constexpr std::size_t OriginByte = 0x31;
constexpr std::uint8_t GivenOrigin = 2;
constexpr std::uint8_t GuildWarOrigin = 1;
constexpr std::size_t GiverByte = 0x32;
constexpr std::size_t KillCountByte = 0x33;

// 0x5b9403..0x5b949c: los floats de la absorción.
constexpr float AbsorbManaFactor = 0.02f;    // 0x6b8b90
constexpr float AbsorbAgilityFactor = 0.02f; // 0x6b89e4
constexpr float AbsorbEnergyFactor = 0.005f; // 0x6ba32c
constexpr float AbsorbBase = 10.0f;          // 0x6b87a8
constexpr float TimeEnergyFactor = 0.025f;   // 0x6ba1fc
constexpr float TimeBase = 60.0f;            // 0x6b86cc

// 0x5b97b1: el máximo del daño de los stats.
constexpr std::uint32_t StatDamageSpread = 0x1e;

// Lo que comparten los tramos de 0x5b90b0: la skill ([EBP−8]), la clase ([EBP−0x10]), el cuarto argumento, los
// stats con lo de los ítems (DI, SI, BX, [EBP−4] y [EBP−0x14]), los daños y lo que dejó 0x551c70.
struct SkillFrame
{
    GameHudContext& context;
    TooltipWriter& lines;
    const OwnCharacterRecord& own;
    const ActorState& hero;
    std::uint8_t skill = 0;
    std::uint32_t heroClass = 0;
    std::int32_t siegeSkill = 0;
    std::uint32_t strength = 0;
    std::uint32_t agility = 0;
    std::uint32_t vitality = 0;
    std::uint32_t energy = 0;
    std::uint32_t leadership = 0;
    // [EBP−0x3c] y [EBP−0x38]: 0x5513d0.
    OwnMagicDamage magic{};
    // [EBP−0x1c] y [EBP−0x24]: 0x5516e0 más 0x5b8b80.
    std::int32_t damageMin = 0;
    std::int32_t damageMax = 0;
    // El nombre, el maná ([EBP−0x2c]), el alcance ([EBP−0x48]) y la stamina ([EBP−0x30]).
    SkillEntryFields entry{};
};

Core::Expected<void, std::string> Checked(const TooltipWriter& lines)
{
    if (lines.Failed())
        return Core::Unexpected(lines.Error());
    return {};
}

// 0x5b93b0..0x5b93db: un stat más lo que le suman los ítems, en 16 bits.
std::uint32_t StatOf(std::uint16_t base, std::uint16_t items)
{
    return static_cast<std::uint16_t>(base + items);
}

std::int32_t Wrapped(std::uint32_t value)
{
    return static_cast<std::int32_t>(value);
}

// La entrada `skill` de Skill.bmd, sin abrir su acceso.
Core::Expected<std::span<const std::byte, Data::Tables::SkillRecordSize>, std::string>
SkillEntryOf(const GameHudContext& context, std::uint32_t skill, std::string_view where)
{
    if (context.skills == nullptr)
        return Core::Unexpected(std::format("{}: sin Skill.bmd", where));
    const auto record = context.skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::format("{}: Skill.bmd no tiene la skill {}", where, skill));
    return *record;
}

std::uint8_t EntryByte(std::span<const std::byte, Data::Tables::SkillRecordSize> entry, std::size_t offset)
{
    return std::to_integer<std::uint8_t>(entry[offset]);
}

// 0x552470 y 0x5519f0: un byte de la entrada `skill`, con su acceso (60 rand() si su cuenta vuelve a 0).
Core::Expected<std::uint8_t, std::string> ReadTheEntryByte(GameHudContext& context, std::uint8_t skill,
                                                           std::size_t offset, std::string_view where)
{
    auto entry = SkillEntryOf(context, skill, where);
    if (!entry.HasValue())
        return Core::Unexpected(entry.Error());
    const Render::Models::LogicalAccess access(context.world.Runtime().movement.skillDepths[skill], context.random,
                                               static_cast<std::uint16_t>(Data::Tables::SkillRecordSize));
    return EntryByte(entry.Value(), offset);
}

// La caja arriba de y, con cy del primer carácter del renglón 0 (GetTextExtentPointA sobre la fuente del DC) y la
// altura ((renglones − medios) · cy + medios · cy / 2) / ry por _ftol.
Core::Expected<void, std::string> DrawTheBoxAbove(GameHudContext& context, std::int32_t x, std::int32_t y,
                                                  std::int32_t count, std::int32_t halves, std::string_view where)
{
    const TooltipLines& lines = context.hud.tooltip;
    const std::string_view first = std::string_view(lines.text[0]).substr(0, 1);
    const std::int32_t cy = context.text.Measure(context.hud.dcFont, first).height;
    const std::int32_t sum = (count - halves) * cy + halves * cy / 2;
    const double ry = Render::ScaleY(1.0f, context.sprites.Screen());
    const std::optional<std::int32_t> height = World::OriginalFtolLow(static_cast<double>(sum) / ry);
    if (!height)
        return Core::Unexpected(std::format("{}: el alto del tooltip ({} / {}) no entra en _ftol", where, sum, ry));
    const auto top = static_cast<std::int32_t>(static_cast<std::uint32_t>(y) - static_cast<std::uint32_t>(*height));
    return DrawTheTooltipBox(context, x, top, count, 0, CenteredMode, WithFrame);
}

// 0x421c10(x, y, lugar), cdecl: con un lugar de 0x78 a 0x7b y el Dark Lord, el nombre del comando en azul y negrita,
// dos "\n" y su ayuda, y la caja; devuelve 1. Solo escribe el color del renglón 1 y el color y la negrita del 0: los
// otros quedan como estaban (Q-C720).
Core::Expected<bool, std::string> DrawTheCommandTooltip(GameHudContext& context, std::int32_t x, std::int32_t y,
                                                        std::int32_t slot, std::uint32_t heroClass)
{
    if (slot < FirstCommand || slot >= FirstCommand + CommandCount || heroClass != DarkLordClass)
        return false;
    const auto command = static_cast<std::size_t>(slot - FirstCommand);
    std::int32_t count = 0;
    TooltipWriter lines(context.hud.tooltip, context.texts, count);
    lines.At("0x421c6d");
    lines.Color(TooltipBlue);
    lines.Bold(1);
    lines.PrintText(CommandNameText + command);
    lines.Next();
    lines.Color(TooltipWhite);
    lines.Print(NewLineFormat);
    lines.Next();
    lines.Print(NewLineFormat);
    lines.Next();
    lines.At("0x421cd4");
    lines.PrintText(CommandHelpText + command);
    lines.Next();
    if (auto checked = Checked(lines); !checked.HasValue())
        return Core::Unexpected(checked.Error());
    if (auto drawn = DrawTheBoxAbove(context, x, y, CommandLines, CommandHalves, "0x421d27"); !drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    return true;
}

// 0x5b9239..0x5b93a9: "\n", el nombre en azul y negrita (el 1200 con la 0x3c y la opción 0x42 en la derecha) y "\n".
Core::Expected<void, std::string> WriteTheName(SkillFrame& frame)
{
    TooltipWriter& lines = frame.lines;
    std::string_view name = frame.entry.name;
    if (frame.skill == ForceSkill && frame.hero.hands[0].model != NoModel)
    {
        const World::ItemState& right = frame.own.equipment[0];
        for (std::size_t i = 0; i < right.optionCount; ++i)
        {
            if (i >= right.optionBytes.size())
                return Core::Unexpected(std::format("0x5b9271: la opción {} del lugar 0 del registro propio lee fuera "
                                                    "de sus {} bytes (D+0x3c en adelante)",
                                                    i, right.optionBytes.size()));
            if (right.optionBytes[i] != ForceWaveOption)
                continue;
            lines.At("0x5b934f");
            name = lines.Text(ForceWaveText);
            if (auto checked = Checked(lines); !checked.HasValue())
                return checked;
            if (name.size() >= NameBufferBytes)
                return Core::Unexpected(std::format("0x5b934f: el texto {} ({} bytes) no entra en los 256 del buffer "
                                                    "del nombre (wsprintfA sigue por la pila)",
                                                    ForceWaveText, name.size()));
            break;
        }
    }
    lines.At("0x5b9362");
    lines.Print(NewLineFormat);
    lines.Next();
    lines.Print(TextFormat, name);
    lines.Color(TooltipBlue);
    lines.Bold(1);
    lines.Next();
    lines.Print(NewLineFormat);
    lines.Next();
    return Checked(lines);
}

// 0x5b93f7..0x5b94c0: la absorción del Wizard, con la x87 en 53 bits: el 880 se calcula primero y el 881 al final.
Core::Expected<void, std::string> WriteTheAbsorption(SkillFrame& frame)
{
    TooltipWriter& lines = frame.lines;
    const double energy = static_cast<double>(frame.energy);
    const std::optional<std::int32_t> mana =
        World::OriginalFtolLow(static_cast<double>(frame.own.maxMana) * static_cast<double>(AbsorbManaFactor));
    if (!mana)
        return Core::Unexpected(std::string("0x5b9409: el maná de la absorción no entra en _ftol"));
    const std::optional<std::int32_t> absorb =
        World::OriginalFtolLow(static_cast<double>(frame.agility) * static_cast<double>(AbsorbAgilityFactor) +
                               energy * static_cast<double>(AbsorbEnergyFactor) + static_cast<double>(AbsorbBase));
    if (!absorb)
        return Core::Unexpected(std::string("0x5b9447: la absorción no entra en _ftol"));
    lines.At("0x5b9457");
    lines.Line(TooltipWhite, AbsorbText, *absorb);
    lines.At("0x5b9479");
    lines.Line(TooltipWhite, AbsorbManaText, *mana);
    const std::optional<std::int32_t> time =
        World::OriginalFtolLow(energy * static_cast<double>(TimeEnergyFactor) + static_cast<double>(TimeBase));
    if (!time)
        return Core::Unexpected(std::string("0x5b949c: la duración de la absorción no entra en _ftol"));
    lines.At("0x5b94ac");
    lines.Line(TooltipWhite, AbsorbTimeText, *time);
    return Checked(lines);
}

// 0x5b93de..0x5b957e: el daño según la clase.
Core::Expected<void, std::string> WriteTheDamage(SkillFrame& frame)
{
    TooltipWriter& lines = frame.lines;
    if (frame.heroClass == WizardClass)
    {
        if (frame.skill == AbsorbSkill)
            return WriteTheAbsorption(frame);
        // Q-C722: el que llama desde el asedio pasa la skill de asedio en el cuarto argumento.
        if (frame.siegeSkill >= FirstSiegeSkill && frame.siegeSkill <= LastSiegeSkill)
            return {};
        lines.At("0x5b956a");
        lines.Line(TooltipWhite, MagicDamageText, frame.magic.min, frame.magic.max);
        return Checked(lines);
    }
    if (frame.heroClass != KnightClass && frame.heroClass != ElfClass && frame.heroClass != GladiatorClass &&
        frame.heroClass != DarkLordClass)
        return {};
    if (frame.skill == PetSkill)
    {
        lines.At("0x5b953a");
        lines.Line(TooltipWhiteOnBlue, PetText);
        return Checked(lines);
    }
    if (std::ranges::find(NoDamageSkills, frame.skill) != NoDamageSkills.end())
        return {};
    lines.At("0x5b956a");
    lines.Line(TooltipWhite, SkillDamageText, frame.damageMin, frame.damageMax);
    return Checked(lines);
}

// 0x5b958a..0x5b97ea: con la 0x4c, el byte +0x21 de su entrada (0x552470, con su acceso) más los stats de la clase, y
// ese daño hasta 30 más; las clases 5 a 7 muestran el de la skill.
Core::Expected<void, std::string> WriteTheStatDamage(SkillFrame& frame)
{
    if (frame.skill != StatDamageSkill)
        return {};
    auto bonus = ReadTheEntryByte(frame.context, StatDamageSkill, DamageByte, "0x552470");
    if (!bonus.HasValue())
        return Core::Unexpected(bonus.Error());
    const std::uint32_t added = bonus.Value();
    const std::uint32_t strength = frame.strength;
    const std::uint32_t agility = frame.agility;
    const std::uint32_t vitality = frame.vitality;
    const std::uint32_t energy = frame.energy;
    std::int32_t damage = frame.damageMin;
    if (frame.heroClass == KnightClass || frame.heroClass == GladiatorClass)
        damage = Wrapped(strength / 3 + agility / 5 + vitality / 5 + energy / 7 + added);
    else if (frame.heroClass == WizardClass)
        damage = Wrapped(strength / 5 + agility / 5 + vitality / 7 + energy / 3 + added);
    else if (frame.heroClass == ElfClass)
        damage = Wrapped(strength / 5 + agility / 3 + vitality / 7 + energy / 5 + added);
    else if (frame.heroClass == DarkLordClass)
        damage = Wrapped(strength / 5 + agility / 5 + vitality / 7 + energy / 3 + frame.leadership / 3 + added);
    const std::int32_t maximum = Wrapped(static_cast<std::uint32_t>(damage) + StatDamageSpread);
    TooltipWriter& lines = frame.lines;
    lines.At("0x5b97cc");
    lines.Line(TooltipWhite, SkillDamageText, damage, maximum);
    return Checked(lines);
}

// 0x5b97ed..0x5b98d4: lo de la energía en las tres de la elfa.
Core::Expected<void, std::string> WriteTheElfLines(SkillFrame& frame)
{
    if (frame.heroClass != ElfClass)
        return {};
    TooltipWriter& lines = frame.lines;
    const auto energy = static_cast<std::int32_t>(frame.energy);
    lines.At("0x5b98b8");
    if (frame.skill == HealSkill)
        lines.Line(TooltipWhite, HealText, energy / 5 + 5);
    else if (frame.skill == DefenseSkill)
        lines.Line(TooltipWhite, DefenseText, (energy >> 3) + 2);
    else if (frame.skill == AttackSkill)
        lines.Line(TooltipWhite, AttackText, energy / 7 + 3);
    return Checked(lines);
}

// 0x5b98d7..0x5b9976: el alcance (si no es 0), el maná y la stamina (si es más de 0), de 0x551c70.
Core::Expected<void, std::string> WriteTheCosts(SkillFrame& frame)
{
    TooltipWriter& lines = frame.lines;
    if (frame.entry.range != 0)
    {
        lines.At("0x5b98f5");
        lines.Line(TooltipWhite, RangeText, static_cast<std::int32_t>(frame.entry.range));
    }
    lines.At("0x5b992e");
    lines.Line(TooltipWhite, ManaText, static_cast<std::int32_t>(frame.entry.mana));
    if (frame.entry.stamina > 0)
    {
        lines.At("0x5b9960");
        lines.Line(TooltipWhite, StaminaText, static_cast<std::int32_t>(frame.entry.stamina));
    }
    return Checked(lines);
}

bool IsComboSkill(std::uint8_t skill)
{
    return (skill >= FirstComboSkill && skill <= LastComboSkill) ||
           std::ranges::find(OtherComboSkills, skill) != OtherComboSkills.end();
}

// 0x5b9977..0x5b9a2c: el Dark Knight, el 96 con la 0x2f y el combo (99) con el +0x390 del héroe en 1, nivel 220 o
// más y una skill del combo.
Core::Expected<void, std::string> WriteTheKnightLines(SkillFrame& frame)
{
    if (frame.heroClass != KnightClass)
        return {};
    TooltipWriter& lines = frame.lines;
    if (frame.skill == NotMountedSkill)
    {
        lines.At("0x5b99b0");
        lines.Line(TooltipWhiteOnBlue, NotMountedText);
    }
    if (frame.hero.byte390 == ComboQuestDone && frame.own.level >= ComboLevel && IsComboSkill(frame.skill))
    {
        lines.At("0x5b9a12");
        lines.Line(TooltipWhiteOnBlue, ComboText);
    }
    return Checked(lines);
}

// 0x5b9a2d..0x5b9a8d: el atributo de la skill (0x5519f0, el byte +0x30 con su acceso); sin 0xff, su texto (1080 +
// atributo) como formato y sin argumentos, en azul.
Core::Expected<void, std::string> WriteTheAttribute(SkillFrame& frame)
{
    if (auto checked = Checked(frame.lines); !checked.HasValue())
        return checked;
    auto attribute = ReadTheEntryByte(frame.context, frame.skill, AttributeByte, "0x5519f0");
    if (!attribute.HasValue())
        return Core::Unexpected(attribute.Error());
    if (attribute.Value() == NoAttribute)
        return {};
    TooltipWriter& lines = frame.lines;
    lines.At("0x5b9a73");
    lines.Line(TooltipBlue, FirstAttributeText + attribute.Value());
    return Checked(lines);
}

// 0x5b9c94..0x5b9f58: la skill que la dio, con su nombre (sprintf "%s" del principio de su entrada) y su byte +0x21,
// con el acceso de su entrada adentro del de la skill.
Core::Expected<void, std::string> WriteTheGiver(SkillFrame& frame, std::uint8_t giver)
{
    GameHudContext& context = frame.context;
    auto entry = SkillEntryOf(context, giver, "0x5b9ccf");
    if (!entry.HasValue())
        return Core::Unexpected(entry.Error());
    const std::span<const std::byte, Data::Tables::SkillRecordSize> record = entry.Value();
    const Render::Models::LogicalAccess access(context.world.Runtime().movement.skillDepths[giver], context.random,
                                               static_cast<std::uint16_t>(Data::Tables::SkillRecordSize));
    std::string name;
    for (const std::byte value : record)
    {
        if (value == std::byte{0})
            break;
        name.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
    }
    if (name.size() == record.size())
        return Core::Unexpected(std::format("0x5b9ee6: el nombre de la skill {} no tiene '\\0' en su entrada (sprintf "
                                            "sigue por la que viene)",
                                            giver));
    TooltipWriter& lines = frame.lines;
    lines.At("0x5b9ee6");
    lines.Line(TooltipWhiteOnBlue, GiverText, std::string_view(name));
    lines.At("0x5b9f26");
    lines.Line(TooltipWhiteOnBlue, GiverDelayText, static_cast<std::int32_t>(EntryByte(record, DamageByte)));
    return Checked(lines);
}

// 0x5b9a8e..0x5ba3c4: el acceso en línea de la entrada de la skill y lo que dice su +0x31.
Core::Expected<void, std::string> WriteTheOrigin(SkillFrame& frame)
{
    if (auto checked = Checked(frame.lines); !checked.HasValue())
        return checked;
    GameHudContext& context = frame.context;
    auto entry = SkillEntryOf(context, frame.skill, "0x5b9ab8");
    if (!entry.HasValue())
        return Core::Unexpected(entry.Error());
    const std::span<const std::byte, Data::Tables::SkillRecordSize> record = entry.Value();
    const Render::Models::LogicalAccess access(context.world.Runtime().movement.skillDepths[frame.skill],
                                               context.random,
                                               static_cast<std::uint16_t>(Data::Tables::SkillRecordSize));
    if (EntryByte(record, OriginByte) == GivenOrigin)
    {
        if (auto given = WriteTheGiver(frame, EntryByte(record, GiverByte)); !given.HasValue())
            return given;
    }
    if (EntryByte(record, OriginByte) == GuildWarOrigin)
    {
        TooltipWriter& lines = frame.lines;
        lines.At("0x5ba164");
        lines.Line(TooltipWhiteOnBlue, GuildWarText);
        lines.At("0x5ba1a5");
        lines.Line(TooltipWhiteOnBlue, KillCountText, static_cast<std::int32_t>(EntryByte(record, KillCountByte)));
    }
    return Checked(frame.lines);
}

// 0x5ba3c4..0x5ba4d2: la 0x3f del Dark Lord sin party, las dos notas de la 0x4c y el "\n" del final, sin color ni
// negrita.
Core::Expected<void, std::string> WriteTheTail(SkillFrame& frame)
{
    TooltipWriter& lines = frame.lines;
    if (frame.heroClass == DarkLordClass && frame.skill == PartySkill && frame.context.hud.partyCount <= 0)
    {
        lines.At("0x5ba403");
        lines.Line(TooltipWhiteOnBlue, PartyText);
    }
    if (frame.skill == StatDamageSkill)
    {
        lines.At("0x5ba444");
        lines.Line(TooltipWhiteOnBlue, FirstStatNoteText);
        lines.At("0x5ba475");
        lines.Line(TooltipWhiteOnBlue, SecondStatNoteText);
    }
    lines.At("0x5ba4ba");
    lines.Print(NewLineFormat);
    lines.Next();
    return Checked(lines);
}

// 0x5b91a1..0x5ba4d2, con el registro propio abierto: los daños, la entrada y los tramos.
Core::Expected<void, std::string> WriteTheSkill(GameHudContext& context, TooltipWriter& lines, const ActorState& hero,
                                                std::uint32_t heroClass, std::int32_t slot, std::int32_t siegeSkill)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    const OwnCharacterRecord& own = world.OwnCharacter();
    const std::optional<std::uint8_t> skill = slot < 0 ? std::nullopt : own.SkillAt(static_cast<std::size_t>(slot));
    if (!skill)
        return Core::Unexpected(std::format("0x5b91af: el byte +0x6c + {} del registro propio no está en MuRet", slot));
    std::uint8_t& depth = runtime.movement.skillDepths[*skill];
    // Un dibujo intermedio no deja las marcas de la lista de los conjuntos: trabaja sobre una copia.
    ItemSetOptionList projected = world.ItemSets().list;
    ItemSetOptionList& setOptions = context.projecting ? projected : world.ItemSets().list;
    auto magic = MagicDamageOf(own, *skill, context.skills, depth, context.random, setOptions);
    if (!magic.HasValue())
        return Core::Unexpected(magic.Error());
    auto damage = SkillDamageOf(*skill, context.skills, depth, context.random, setOptions);
    if (!damage.HasValue())
        return Core::Unexpected(damage.Error());
    // 0x5b91ee: adentro del registro propio, su acceso no saca nada.
    const OwnAttackDamage attack =
        AttackDamageOf(own, heroClass, HeldItemOf(runtime), context.ownRecordDepth, context.random);
    auto entry = ReadTheSkillEntry(context, *skill, "0x551c70");
    if (!entry.HasValue())
        return Core::Unexpected(entry.Error());
    if (entry.Value().name.size() == Data::Tables::SkillRecordSize)
        return Core::Unexpected(std::format("0x551e0c: el nombre de la skill {} no tiene '\\0' en su entrada (strcpy "
                                            "sigue por la que viene)",
                                            *skill));
    SkillFrame frame{
        .context = context,
        .lines = lines,
        .own = own,
        .hero = hero,
        .skill = *skill,
        .heroClass = heroClass,
        .siegeSkill = siegeSkill,
        .strength = StatOf(own.strength, own.word2a),
        .agility = StatOf(own.agility, own.word2c),
        .vitality = StatOf(own.vitality, own.word2e),
        .energy = StatOf(own.energy, own.word30),
        .leadership = StatOf(own.leadership, own.word36),
        .magic = magic.Value(),
        .damageMin = Wrapped(static_cast<std::uint32_t>(damage.Value().min) + attack.min),
        .damageMax = Wrapped(static_cast<std::uint32_t>(damage.Value().max) + attack.max),
        .entry = entry.Value(),
    };
    using Part = Core::Expected<void, std::string> (*)(SkillFrame&);
    constexpr std::array<Part, 9> Parts{WriteTheName,      WriteTheDamage, WriteTheStatDamage,
                                        WriteTheElfLines,  WriteTheCosts,  WriteTheKnightLines,
                                        WriteTheAttribute, WriteTheOrigin, WriteTheTail};
    for (const Part part : Parts)
    {
        if (auto written = part(frame); !written.HasValue())
            return written;
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawTheSkillTooltip(GameHudContext& context, std::int32_t x, std::int32_t y,
                                                      std::int32_t slot, std::int32_t siegeSkill)
{
    WorldState& world = context.world;
    const ActorState& hero = world.Actors()[world.Runtime().hero];
    const std::uint32_t heroClass = hero.characterClass & ClassBits;
    auto command = DrawTheCommandTooltip(context, x, y, slot, heroClass);
    if (!command.HasValue())
        return Core::Unexpected(command.Error());
    if (command.Value())
        return {};
    // EBX: el renglón que sigue, desde 0. 0x78abb30 y 0x78abb34 quedan como estaban.
    std::int32_t count = 0;
    TooltipWriter lines(context.hud.tooltip, context.texts, count);
    {
        // 0x5b90e6..0x5ba6b3: el registro propio, abierto hasta después del último renglón.
        const Render::Models::LogicalAccess own(context.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        if (auto written = WriteTheSkill(context, lines, hero, heroClass, slot, siegeSkill); !written.HasValue())
            return written;
    }
    return DrawTheBoxAbove(context, x, y, count, SkillHalves, "0x5ba6fb");
}
} // namespace Client::Scenes
