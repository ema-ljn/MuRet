#include "Scenes/PetTooltip.h"

#include "Core/Expected.h"
#include "Protocol/Pet/PetInfo.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/ScreenScale.h"
#include "Render/SpriteBatch.h"
#include "Scenes/GameHud.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameTooltips.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/ItemTooltipParts.h"
#include "Scenes/ItemTooltips.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnItemPlacement.h"
#include "Scenes/PetStats.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/TooltipWriter.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/PanelRegistry.h"
#include "World/ItemState.h"
#include "World/OriginalFtol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
// 0x4229b5..0x4229c1: los dos tipos; con otro, 0x422990 devuelve 0.
constexpr std::uint16_t DarkHorseItem = 0x1a04;
constexpr std::uint16_t DarkRavenItem = 0x1a05;

// 0x4229da..0x4229e8: la clave del pedido, ((y & 0xffff) << 16) | (x & 0xffff).
constexpr std::uint32_t KeyMask = 0xffff;
constexpr std::uint32_t KeyShift = 16;

// Los textos de Text.bmd (la tabla de 0x76c1bb0 − 63 · 300): el nombre del caballo y el del cuervo (0x77140e0 y
// 0x7716084), la experiencia (0x76cbd68), el nivel (0x76d811c, con "%s : %d" de 0x6c707c), el daño y la velocidad del
// cuervo (0x76cbfc0 y 0x76c1cdc), la durabilidad (0x76c23e4), el nivel pedido (0x76c2aec), lo que falta (0x76c2894),
// el liderazgo pedido (0x76f03d4), las clases (0x76c1958 con 0x76bedfc), la defensa del caballo (0x76f39bc) y su
// skill (0x771420c).
constexpr std::size_t HorseNameText = 1187;
constexpr std::size_t RavenNameText = 1214;
constexpr std::size_t ExperienceText = 201;
constexpr std::size_t LevelText = 368;
constexpr char LevelFormat[] = "%s : %d";
constexpr std::size_t DamageText = 203;
constexpr std::size_t SpeedText = 64;
constexpr std::size_t DurabilityText = 70;
constexpr std::size_t LevelRequirementText = 76;
constexpr std::size_t MissingText = 74;
constexpr std::size_t LeadershipRequirementText = 698;
constexpr std::size_t ClassesText = 61;
constexpr std::size_t DarkLordText = 24;
constexpr std::size_t HorseDefenseText = 744;
constexpr std::size_t HorseSkillText = 1188;
// 0x42306d..0x423082: el nivel pedido del caballo, nivel · 2 + 0xda; 0x4230b9..0x4230ce: el liderazgo pedido del
// cuervo, nivel · 15 + 0xb9.
constexpr std::int32_t LevelRequirementBase = 0xda;
constexpr std::int32_t LevelRequirementFactor = 2;
constexpr std::int32_t LeadershipRequirementBase = 0xb9;
constexpr std::int32_t LeadershipRequirementFactor = 15;
// 0x423b6d..0x423b7c: la defensa, (nivel + 30) / 2 con signo; 0x423ba7: la skill, con 2.
constexpr std::int32_t HorseDefenseBase = 0x1e;
constexpr std::int32_t HorseDefenseDivisor = 2;
constexpr std::int32_t HorseSkillArgument = 2;
// 0x423c3f..0x423c41: el modo centrado de 0x467060 y el marco de 0x5af090.
constexpr std::int32_t CenteredMode = 2;
constexpr std::int32_t WithFrame = 1;

using PetTable = std::array<PetStats, PetStatsCount>;

// 0x423c65..0x42403d: anota el ítem y pide su A9 por la sesión del juego, como el botón de la mascota (0x424610).
void AskForThePet(GameHudContext& hud, std::uint32_t key, const World::ItemState& item, std::uint8_t pet)
{
    WorldRuntime& runtime = hud.world.Runtime();
    runtime.petTooltipQuery = PetTooltipQuery{.key = key, .type = item.id, .scalar = item.scalar1Low};
    const Protocol::Pet::PetInfoRequest request{.pet = pet,
                                                .place = static_cast<std::uint8_t>(runtime.ui.field78abb28),
                                                .slot = static_cast<std::uint8_t>(runtime.ui.field78abb2c)};
    if (hud.send)
        hud.send(Protocol::Pet::WritePetInfoRequest(request));
}

// 0x42324c..0x42350b: el nivel pedido del caballo y, con el registro propio abierto, lo que le falta al héroe.
Core::Expected<void, std::string> WriteTheLevelRequirement(GameHudContext& hud, TooltipWriter& lines,
                                                           std::int32_t required)
{
    lines.At("0x423252");
    lines.PrintText(LevelRequirementText, required);
    if (lines.Failed())
        return Core::Unexpected(lines.Error());
    std::int32_t level = 0;
    {
        const Render::Models::LogicalAccess own(hud.ownRecordDepth, hud.random, Render::Models::OwnRecordCloseRandoms);
        level = hud.world.OwnCharacter().level;
    }
    if (level < required)
    {
        lines.Color(TooltipRed);
        lines.Bold(0);
        lines.Next();
        lines.PrintText(MissingText, required - level);
        lines.Color(TooltipRed);
        lines.Bold(0);
        lines.Next();
    }
    else
    {
        lines.Color(TooltipWhite);
        lines.Bold(0);
        lines.Next();
    }
    return {};
}

// 0x423671..0x423a8b: el liderazgo pedido del cuervo y, con el registro propio abierto, lo que le falta al héroe:
// +0x36 + +0x20 en 16 bits (0x423839..0x423844).
Core::Expected<void, std::string> WriteTheLeadershipRequirement(GameHudContext& hud, TooltipWriter& lines,
                                                                std::int32_t required)
{
    lines.At("0x42367c");
    lines.PrintText(LeadershipRequirementText, required);
    if (lines.Failed())
        return Core::Unexpected(lines.Error());
    std::int32_t leadership = 0;
    {
        const Render::Models::LogicalAccess own(hud.ownRecordDepth, hud.random, Render::Models::OwnRecordCloseRandoms);
        const OwnCharacterRecord& record = hud.world.OwnCharacter();
        leadership = static_cast<std::uint16_t>(record.word36 + record.leadership);
    }
    if (leadership < required)
    {
        lines.Bold(0);
        lines.Color(TooltipRed);
        lines.Next();
        lines.PrintText(MissingText, required - leadership);
        lines.Color(TooltipRed);
    }
    else
        lines.Color(TooltipWhite);
    lines.Bold(0);
    lines.Next();
    return {};
}

// 0x423ae9..0x423b5a: por cada opción, mientras queden (la cuenta se vuelve a leer: 0x4225d0 la sube al agregar su
// 0x96), 0x4225d0 (que con el cuervo vuelve enseguida) y 0x5b2320(tipo, renglón, tag, valor, 0), en azul y sin
// negrita, con un medio renglón. Con la 0x96 en la primera opción cada vuelta agrega otra (Q-C415) y el lazo no
// termina hasta que la cuenta da la vuelta, escribiendo más allá del ítem (Q-C1044): MuRet corta antes de la décima.
Core::Expected<void, std::string> WriteTheOptions(GameHudContext& hud, TooltipWriter& lines, World::ItemState& item,
                                                  std::int32_t& halves)
{
    const std::int32_t type = static_cast<std::int16_t>(item.id);
    for (std::uint8_t i = 0; i < item.optionCount; ++i)
    {
        lines.At("0x423b06");
        if (lines.Failed())
            return Core::Unexpected(lines.Error());
        if (item.id == DarkHorseItem)
        {
            const std::uint8_t place = DarkHorseOptionPlace(item);
            if (place >= World::MaxItemOptions)
                return Core::Unexpected(
                    std::format("0x423b07: la 0x96 del caballo va en la opción {} (D+0x2b + {}), fuera de las {} que "
                                "guarda MuRet (Q-C415, Q-C1044)",
                                place, place, World::MaxItemOptions));
            AddTheDarkHorseOption(item, hud.world, hud.ownRecordDepth, hud.random);
        }
        if (i >= World::MaxItemOptions)
            return Core::Unexpected(
                std::format("0x423b0c: la opción {} de {} lee su valor fuera de D+0x2b..D+0x3b", i, item.optionCount));
        const std::uint8_t tag = item.optionBytes[i];
        const std::uint8_t value = item.optionBytes[World::ItemOptionValueOffset + i];
        if (auto written = WriteAnOption(hud, lines, type, tag, value); !written.HasValue())
            return written;
        lines.Color(TooltipBlue);
        lines.Bold(0);
        lines.Next();
        ++halves;
    }
    return {};
}

// 0x423bf1..0x423c4a: la caja, arriba de (x, y) con (N − M) · cy + M · cy / 2 sobre ry, con cy el de la primera letra
// del renglón 0 en la fuente que tenga el DC; si no entra arriba (la resta da negativo), en y.
Core::Expected<void, std::string> DrawThePetBox(const ItemTooltipContext& context, std::int32_t count,
                                                std::int32_t halves)
{
    GameHudContext& hud = context.hud;
    const TooltipLines& lines = hud.hud.tooltip;
    const std::string_view first = std::string_view(lines.text[0]).substr(0, 1);
    const std::int32_t cy = hud.text.Measure(hud.hud.dcFont, first).height;
    const std::int32_t sum = (count - halves) * cy + halves * cy / 2;
    const double ry = Render::ScaleY(1.0f, hud.sprites.Screen());
    const std::optional<std::int32_t> height = World::OriginalFtolLow(static_cast<double>(sum) / ry);
    if (!height)
        return Core::Unexpected(
            std::format("0x423c2f: el alto del tooltip de la mascota ({} / {}) no entra en _ftol", sum, ry));
    std::int32_t top = context.y;
    const auto raised =
        static_cast<std::int32_t>(static_cast<std::uint32_t>(context.y) - static_cast<std::uint32_t>(*height));
    if (raised >= 0)
        top = raised;
    return DrawTheTooltipBox(hud, context.x, top, count, 0, CenteredMode, WithFrame);
}
} // namespace

Core::Expected<bool, std::string> DrawThePetTooltip(const ItemTooltipContext& context)
{
    World::ItemState& item = context.item;
    if (item.id != DarkHorseItem && item.id != DarkRavenItem)
        return false;
    GameHudContext& hud = context.hud;
    WorldState& world = hud.world;
    WorldRuntime& runtime = world.Runtime();
    const bool horse = item.id == DarkHorseItem;
    // 0x422a1a..0x422a2d y 0x423c7c..0x423c85: el byte de la mascota, 1 el caballo y 0 el cuervo.
    const std::uint8_t pet = horse ? Protocol::Pet::DarkHorsePet : Protocol::Pet::DarkRavenPet;
    // 0x4229d4..0x422a12: otro lugar, otro tipo u otro D+4 que los del último pedido.
    const std::uint32_t key = ((static_cast<std::uint32_t>(context.y) & KeyMask) << KeyShift) |
                              (static_cast<std::uint32_t>(context.x) & KeyMask);
    const PetTooltipQuery& query = runtime.petTooltipQuery;
    if (query.key != key || query.type != item.id || query.scalar != item.scalar1Low)
    {
        if (!hud.projecting)
            AskForThePet(hud, key, item, pet);
        return true;
    }
    // 0x422a35: 0x4221d0(mascota), con el registro propio abierto y cerrado.
    PetTable projected = runtime.pets;
    PetTable& pets = hud.projecting ? projected : runtime.pets;
    UpdatePetStats(pet, world, pets, hud.ownRecordDepth, hud.random);
    // 0x422a3a..0x422b0c: el precio. Con la tienda de NPC abierta (0x10000, 1 rand()), 0x422930 con el formato de
    // 0x5b1130, de la CL9.3. Con la tienda personal propia (0x800) o la ajena (0x20000), el de 0x78abaa4 (0x422b12, la
    // CL9.4), que MuRet deja en 0 (Scenes/ItemTooltips.cpp, CheckTheShops): sin precio, N y M quedan en 0.
    GamePanels panels(world, hud.random);
    UI::PanelRegistry& registry = panels.Registry();
    if (registry.IsOpen(UI::NpcShopPanel))
        return Core::Unexpected(
            std::format("0x422a55: el precio del {:#x} en la tienda de NPC (0x422930 con el formato "
                        "de 0x5b1130) es de la CL9.3 (P-C246)",
                        item.id));
    if (!registry.IsOpen(UI::PersonalShopPanel))
        static_cast<void>(registry.IsOpen(UI::OtherShopPanel));
    // N en EBX y M en [EBP−0x24]: 0x422990 no toca 0x78abb30 ni 0x78abb34.
    std::int32_t next = 0;
    std::int32_t halves = 0;
    TooltipWriter lines(hud.hud.tooltip, hud.texts, next);
    // 0x423040..0x4230f3: el nombre, en azul y en negrita (el color y la negrita van antes de mirar el tipo), y lo que
    // piden: el nivel el caballo, el liderazgo el cuervo.
    lines.At("0x423040");
    lines.Color(TooltipBlue);
    lines.Bold(1);
    std::int32_t levelRequirement = 0;
    std::int32_t leadershipRequirement = 0;
    const PetStats& stats = pets[pet];
    if (horse)
    {
        levelRequirement = static_cast<std::int32_t>(stats.level) * LevelRequirementFactor + LevelRequirementBase;
        lines.PrintText(HorseNameText);
    }
    else
    {
        leadershipRequirement =
            static_cast<std::int32_t>(stats.level) * LeadershipRequirementFactor + LeadershipRequirementBase;
        lines.PrintText(RavenNameText);
    }
    lines.Next();
    ++halves;
    // 0x4230fb..0x423143: dos "\n", el primero en blanco.
    lines.At("0x4230fb");
    lines.Color(TooltipWhite);
    lines.Print("\n");
    lines.Next();
    lines.Print("\n");
    lines.Next();
    // 0x42314d..0x4231a5: la experiencia y la que sigue, y el nivel; 0x4231af..0x42320f: el daño (+0xc, +0xe y +0x12)
    // y la velocidad (+0x10) del cuervo; 0x423212..0x42323b: la durabilidad (+0xa). Todos son medios renglones.
    lines.PrintText(ExperienceText, stats.experience, stats.nextExperience);
    lines.Next();
    lines.Print(LevelFormat, lines.Text(LevelText), std::uint32_t{stats.level});
    lines.Next();
    halves += 4;
    if (!horse)
    {
        lines.PrintText(DamageText, std::uint32_t{stats.word0c}, std::uint32_t{stats.word0e},
                        std::uint32_t{stats.word12});
        lines.Next();
        lines.PrintText(SpeedText, std::uint32_t{stats.word10});
        lines.Next();
        halves += 2;
    }
    lines.PrintText(DurabilityText, std::uint32_t{stats.durability});
    lines.Next();
    ++halves;
    // 0x42323c..0x42324c: el nivel pedido, solo el caballo; 0x423671: el liderazgo pedido, solo el cuervo.
    if (horse && levelRequirement > 0)
    {
        if (auto written = WriteTheLevelRequirement(hud, lines, levelRequirement); !written.HasValue())
            return Core::Unexpected(written.Error());
    }
    if (leadershipRequirement > 0)
    {
        if (auto written = WriteTheLeadershipRequirement(hud, lines, leadershipRequirement); !written.HasValue())
            return Core::Unexpected(written.Error());
    }
    // 0x423a8e..0x423ae4: un "\n" y las clases en el 5, con un medio renglón por cada uno.
    lines.At("0x423a8e");
    lines.Print("\n");
    lines.Next();
    lines.Color(TooltipWhiteOnBlue);
    lines.PrintText(ClassesText, lines.Text(DarkLordText));
    lines.Next();
    halves += 2;
    if (auto written = WriteTheOptions(hud, lines, item, halves); !written.HasValue())
        return Core::Unexpected(written.Error());
    // 0x423b5f..0x423bd5: el caballo, la defensa (en azul, la negrita como estaba) con un medio renglón, y su skill.
    if (horse)
    {
        lines.At("0x423b6a");
        const std::int32_t level = pets[pet].level;
        lines.PrintText(HorseDefenseText, (level + HorseDefenseBase) / HorseDefenseDivisor);
        lines.Color(TooltipBlue);
        lines.Next();
        ++halves;
        lines.PrintText(HorseSkillText, HorseSkillArgument);
        lines.Color(TooltipBlue);
        lines.Bold(0);
        lines.Next();
    }
    if (lines.Failed())
        return Core::Unexpected(lines.Error());
    if (auto drawn = DrawThePetBox(context, next, halves); !drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    return true;
}
} // namespace Client::Scenes
