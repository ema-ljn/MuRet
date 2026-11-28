#include "Tooltips/PetTooltip.h"

#include "Core/Expected.h"
#include "Protocol/Pets/PetInfo.h"
#include "Models/LogicalAccess.h"
#include "Render/ScreenScale.h"
#include "Render/SpriteBatch.h"
#include "Ui/GameHudContext.h"
#include "Hud/GamePanels.h"
#include "Tooltips/GameTooltips.h"
#include "Render/Frame/ITextDrawer.h"
#include "Tooltips/ItemTooltipParts.h"
#include "Items/ItemPrice.h"
#include "Ui/ZenText.h"
#include "Tooltips/ItemTooltips.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Items/OwnItemPlacement.h"
#include "Pets/PetStats.h"
#include "Tooltips/TooltipLines.h"
#include "Tooltips/TooltipWriter.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/PanelRegistry.h"
#include "Math/Ftol.h"
#include "Kernel/ItemState.h"
#include "Ui/GameUiState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Tooltips
{
namespace
{
// 0x4229b5..0x4229c1: the two types; with another, 0x422990 returns 0.
constexpr std::uint16_t DarkHorseItem = 0x1a04;
constexpr std::uint16_t DarkRavenItem = 0x1a05;

// 0x4229da..0x4229e8: the request's key, ((y & 0xffff) << 16) | (x & 0xffff).
constexpr std::uint32_t KeyMask = 0xffff;
constexpr std::uint32_t KeyShift = 16;

// The Text.bmd texts (the table at 0x76c1bb0 − 63 · 300): the horse's and the raven's names (0x77140e0 and
// 0x7716084), the experience (0x76cbd68), the level (0x76d811c, with "%s : %d" of 0x6c707c), the raven's damage and
// speed (0x76cbfc0 and 0x76c1cdc), the durability (0x76c23e4), the required level (0x76c2aec), what is missing
// (0x76c2894), the required leadership (0x76f03d4), the classes (0x76c1958 with 0x76bedfc), the horse's defense
// (0x76f39bc) and its skill (0x771420c).
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
// 0x42306d..0x423082: the horse's required level, level · 2 + 0xda; 0x4230b9..0x4230ce: the raven's required
// leadership, level · 15 + 0xb9.
constexpr std::int32_t LevelRequirementBase = 0xda;
constexpr std::int32_t LevelRequirementFactor = 2;
constexpr std::int32_t LeadershipRequirementBase = 0xb9;
constexpr std::int32_t LeadershipRequirementFactor = 15;
// 0x423b6d..0x423b7c: the defense, (level + 30) / 2 signed; 0x423ba7: the skill, with 2.
constexpr std::int32_t HorseDefenseBase = 0x1e;
constexpr std::int32_t HorseDefenseDivisor = 2;
constexpr std::int32_t HorseSkillArgument = 2;
// 0x423c3f..0x423c41: the centered mode of 0x467060 and the frame of 0x5af090.
constexpr std::int32_t CenteredMode = 2;
constexpr std::int32_t WithFrame = 1;

using PetTable = std::array<Kernel::PetStats, Kernel::PetStatsCount>;

// 0x423c65..0x42403d: notes the item and requests its A9 through the game session, like the pet button (0x424610).
void AskForThePet(Ui::GameHudContext& hud, std::uint32_t key, const Kernel::ItemState& item, std::uint8_t pet)
{
    Kernel::WorldRuntime& runtime = hud.world.Runtime();
    runtime.petTooltipQuery = Kernel::PetTooltipQuery{.key = key, .type = item.id, .scalar = item.scalar1Low};
    const Protocol::Pets::PetInfoRequest request{.pet = pet,
                                                 .place = static_cast<std::uint8_t>(runtime.ui.field78abb28),
                                                 .slot = static_cast<std::uint8_t>(runtime.ui.field78abb2c)};
    if (hud.send)
        hud.send(Protocol::Pets::WritePetInfoRequest(request));
}

// 0x42324c..0x42350b: the horse's required level and, with the own record open, what the hero is missing.
Core::Expected<void, std::string> WriteTheLevelRequirement(Ui::GameHudContext& hud, TooltipWriter& lines,
                                                           std::int32_t required)
{
    lines.At("0x423252");
    lines.PrintText(LevelRequirementText, required);
    if (lines.Failed())
        return Core::Unexpected(lines.Error());
    std::int32_t level = 0;
    {
        const Models::LogicalAccess own(hud.ownRecordDepth, hud.random, Models::OwnRecordCloseRandoms);
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

// 0x423671..0x423a8b: the raven's required leadership and, with the own record open, what the hero is missing: +0x36 +
// +0x20 in 16 bits (0x423839..0x423844).
Core::Expected<void, std::string> WriteTheLeadershipRequirement(Ui::GameHudContext& hud, TooltipWriter& lines,
                                                                std::int32_t required)
{
    lines.At("0x42367c");
    lines.PrintText(LeadershipRequirementText, required);
    if (lines.Failed())
        return Core::Unexpected(lines.Error());
    std::int32_t leadership = 0;
    {
        const Models::LogicalAccess own(hud.ownRecordDepth, hud.random, Models::OwnRecordCloseRandoms);
        const Kernel::OwnCharacterRecord& record = hud.world.OwnCharacter();
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

// 0x423ae9..0x423b5a: for each option, while some remain (the count is reread: 0x4225d0 raises it when adding its
// 0x96), 0x4225d0 (which returns at once with the raven) and 0x5b2320(type, line, tag, value, 0), in blue and without
// bold, with a half line. With 0x96 in the first option each round adds another (Q-C415) and the loop does not end
// until the count wraps around, writing beyond the item (Q-C1044): MuRet cuts before the tenth.
Core::Expected<void, std::string> WriteTheOptions(Ui::GameHudContext& hud, TooltipWriter& lines,
                                                  Kernel::ItemState& item, std::int32_t& halves)
{
    const std::int32_t type = static_cast<std::int16_t>(item.id);
    for (std::uint8_t i = 0; i < item.optionCount; ++i)
    {
        lines.At("0x423b06");
        if (lines.Failed())
            return Core::Unexpected(lines.Error());
        if (item.id == DarkHorseItem)
        {
            const std::uint8_t place = Items::DarkHorseOptionPlace(item);
            if (place >= Kernel::MaxItemOptions)
                return Core::Unexpected(
                    std::format("0x423b07: la 0x96 del caballo va en la opción {} (D+0x2b + {}), fuera de las {} que "
                                "guarda MuRet (Q-C415, Q-C1044)",
                                place, place, Kernel::MaxItemOptions));
            Items::AddTheDarkHorseOption(item, hud.world, hud.ownRecordDepth, hud.random);
        }
        if (i >= Kernel::MaxItemOptions)
            return Core::Unexpected(
                std::format("0x423b0c: la opción {} de {} lee su valor fuera de D+0x2b..D+0x3b", i, item.optionCount));
        const std::uint8_t tag = item.optionBytes[i];
        const std::uint8_t value = item.optionBytes[Kernel::ItemOptionValueOffset + i];
        if (auto written = WriteAnOption(hud, lines, type, tag, value); !written.HasValue())
            return written;
        lines.Color(TooltipBlue);
        lines.Bold(0);
        lines.Next();
        ++halves;
    }
    return {};
}

// 0x423bf1..0x423c4a: the box, above (x, y) with (N − M) · cy + M · cy / 2 over ry, with cy that of the first letter of
// line 0 in whatever font the DC has; if it does not fit above (the subtraction is negative), at y.
Core::Expected<void, std::string> DrawThePetBox(const ItemTooltipContext& context, std::int32_t count,
                                                std::int32_t halves)
{
    Ui::GameHudContext& hud = context.hud;
    const TooltipLines& lines = hud.hud.tooltip;
    const std::string_view first = std::string_view(lines.text[0]).substr(0, 1);
    const std::int32_t cy = hud.text.Measure(hud.hud.dcFont, first).height;
    const std::int32_t sum = (count - halves) * cy + halves * cy / 2;
    const double ry = Render::ScaleY(1.0f, hud.sprites.Screen());
    const std::optional<std::int32_t> height = Math::OriginalFtolLow(static_cast<double>(sum) / ry);
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
    Kernel::ItemState& item = context.item;
    if (item.id != DarkHorseItem && item.id != DarkRavenItem)
        return false;
    Ui::GameHudContext& hud = context.hud;
    Kernel::WorldState& world = hud.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    const bool horse = item.id == DarkHorseItem;
    // 0x422a1a..0x422a2d and 0x423c7c..0x423c85: the pet's byte, 1 the horse and 0 the raven.
    const std::uint8_t pet = horse ? Protocol::Pets::DarkHorsePet : Protocol::Pets::DarkRavenPet;
    // 0x4229d4..0x422a12: another place, another type or another D+4 than the last request's.
    const std::uint32_t key = ((static_cast<std::uint32_t>(context.y) & KeyMask) << KeyShift) |
                              (static_cast<std::uint32_t>(context.x) & KeyMask);
    const Kernel::PetTooltipQuery& query = runtime.petTooltipQuery;
    if (query.key != key || query.type != item.id || query.scalar != item.scalar1Low)
    {
        if (!hud.projecting)
            AskForThePet(hud, key, item, pet);
        return true;
    }
    // 0x422a35: 0x4221d0(pet), with the own record opened and closed.
    PetTable projected = runtime.pets;
    PetTable& pets = hud.projecting ? projected : runtime.pets;
    Pets::UpdatePetStats(pet, world, pets, hud.ownRecordDepth, hud.random);
    // 0x422a3a..0x42303d: the prices share the item body's providers, but N and M are local.
    std::int32_t next = 0;
    std::int32_t halves = 0;
    TooltipWriter lines(hud.hud.tooltip, hud.texts, next, halves);
    Hud::GamePanels panels(world, hud.random);
    Ui::PanelRegistry& registry = panels.Registry();
    if (registry.IsOpen(Ui::NpcShopPanel))
    {
        constexpr std::uint32_t SaleDivisor = 3;
        constexpr std::uint32_t PriceRounding = 100;
        constexpr std::size_t PriceText = 63;
        const Items::PetPriceLevels levels{.raven = pets[Protocol::Pets::DarkRavenPet].level,
                                           .horse = pets[Protocol::Pets::DarkHorsePet].level};
        const std::uint32_t price = Items::PetBasePrice(item.id, levels) / SaleDivisor / PriceRounding * PriceRounding;
        lines.At("0x422a55");
        lines.PrintText(PriceText, std::string_view(Ui::FormatZen(price)));
        lines.Color(TooltipWhite);
        lines.Bold(1);
        lines.Next();
        lines.NewLine();
    }
    else if (registry.IsOpen(Ui::PersonalShopPanel) || registry.IsOpen(Ui::OtherShopPanel))
    {
        if (auto written = WriteThePersonalShopPrice(hud, lines, item); !written.HasValue())
            return Core::Unexpected(written.Error());
    }
    if (lines.Failed())
        return Core::Unexpected(lines.Error());
    // 0x423040..0x4230f3: the name, in blue and bold (the color and the bold go before looking at the type), and what
    // they require: the horse the level, the raven the leadership.
    lines.At("0x423040");
    lines.Color(TooltipBlue);
    lines.Bold(1);
    std::int32_t levelRequirement = 0;
    std::int32_t leadershipRequirement = 0;
    const Kernel::PetStats& stats = pets[pet];
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
    // 0x4230fb..0x423143: two "\n", the first in white.
    lines.At("0x4230fb");
    lines.Color(TooltipWhite);
    lines.Print("\n");
    lines.Next();
    lines.Print("\n");
    lines.Next();
    // 0x42314d..0x4231a5: the experience and the next one, and the level; 0x4231af..0x42320f: the raven's damage (+0xc,
    // +0xe and +0x12) and speed (+0x10); 0x423212..0x42323b: the durability (+0xa). All are half lines.
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
    // 0x42323c..0x42324c: the required level, only the horse; 0x423671: the required leadership, only the raven.
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
    // 0x423a8e..0x423ae4: a "\n" and the classes in color 5, with a half line for each.
    lines.At("0x423a8e");
    lines.Print("\n");
    lines.Next();
    lines.Color(TooltipWhiteOnBlue);
    lines.PrintText(ClassesText, lines.Text(DarkLordText));
    lines.Next();
    halves += 2;
    if (auto written = WriteTheOptions(hud, lines, item, halves); !written.HasValue())
        return Core::Unexpected(written.Error());
    // 0x423b5f..0x423bd5: the horse, the defense (in blue, the bold as it was) with a half line, and its skill.
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
} // namespace Client::Tooltips
