#include "Scenes/PartyActors.h"

#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameUi.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/PanelRegistry.h"
#include "UI/Widget.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Client::Scenes
{
namespace
{
// +0x1c de un lugar: −1 no está en el viewport, −2 sin buscar y −3 el héroe publicado.
constexpr std::int32_t NotInView = -1;
constexpr std::int32_t NotSearched = -2;
constexpr std::int32_t TheHero = -3;
// 0x59e3cb: el tipo de los jugadores; 0x59e3d7: +0x88 en 1.
constexpr std::uint16_t PlayerType = 0x20a;
constexpr std::uint8_t PlayerKind = 1;
// 0x59e4fa..0x59e500: 400 registros de 0x438.
constexpr std::size_t SearchedActors = 400;

// 0x5e417f..0x5e4197: la x de la lista (el ancho menos 50, 0x6b86b0) y su primer renglón (5); 0x6b8870, el renglón.
constexpr double ListFromTheRight = 50.0;
constexpr float ListTop = 5.0f;
constexpr double RowHeight = 19.0;
// 0x5e446d (0x6b86b8) y 0x5e4481 (0x6b86b0): el mouse desde x − 3 hasta x + 50.
constexpr double HoverLeft = 3.0;
constexpr double HoverRight = 50.0;
// 0x5e444b..0x5e4462: las skills que eligen desde la lista.
constexpr std::array<std::uint8_t, 5> ListSkills{0x1a, 0x1b, 0x1c, 0x0f, 0x10};
// 0x5e417f: el Dark Wizard (0) y el Elf (2).
constexpr std::uint8_t DarkWizard = 0;
constexpr std::uint8_t Elf = 2;
// 0x6c8e30.
constexpr std::size_t ChosenTarget = 2;

float F32(double value)
{
    return static_cast<float>(value);
}

// Un nombre hasta su primer 0: diez bytes en +0x1d9 (con +0x1e3 en 0, que ponen el 0x12 y el F3 03; *inferido*) o los
// once del lugar, con el 0 de +0xa.
std::string_view NameView(std::span<const std::byte> name)
{
    const auto end = std::ranges::find(name, std::byte{0});
    return {reinterpret_cast<const char*>(name.data()), static_cast<std::size_t>(end - name.begin())};
}

// 0x78aba48. Con más de cinco, el 42 ya cortó (0x4d67e0, Q-C960); negativa, los lazos no corren.
std::size_t Members(const WorldRuntime& runtime)
{
    return static_cast<std::size_t>(std::clamp<std::int32_t>(runtime.hud.partyCount, 0, PartySlots));
}
} // namespace

void MatchThePartyMembers(WorldState& world, std::size_t index)
{
    WorldRuntime& runtime = world.Runtime();
    const ActorState& actor = world.Actors()[index];
    const std::string_view name = NameView(actor.name);
    for (std::size_t at = 0; at < Members(runtime); ++at)
    {
        PartySlot& slot = runtime.social.party.slots[at];
        if (slot.actorIndex != NotSearched)
            continue;
        slot.actorFlags = 0;
        if (NameView(slot.name) != name)
            continue;
        slot.actorIndex = static_cast<std::int32_t>(index);
        slot.actorFlags = actor.flags78;
        return;
    }
}

void MarkTheMissingMembers(WorldState& world)
{
    WorldRuntime& runtime = world.Runtime();
    const std::string_view hero = NameView(world.Actors()[runtime.hero].name);
    for (std::size_t at = 0; at < Members(runtime); ++at)
    {
        PartySlot& slot = runtime.social.party.slots[at];
        if (slot.actorIndex >= 0)
            continue;
        slot.actorFlags = 0;
        slot.actorIndex = NameView(slot.name) != hero ? NotInView : TheHero;
    }
}

void FindThePartyInTheViewport(WorldState& world)
{
    const auto actors = world.Actors();
    for (std::size_t index = 0; index < SearchedActors; ++index)
    {
        const ActorState& actor = actors[index];
        if (actor.type != PlayerType || actor.byte88 != PlayerKind || !actor.alive || !actor.seen ||
            !(actor.alpha > 0.0f) || actor.dying != 0)
            continue;
        // 0x59e420: con la party vacía, el lazo de los lugares no corre.
        MatchThePartyMembers(world, index);
    }
    MarkTheMissingMembers(world);
}

bool PickFromThePartyList(const FramePickingContext& context, UI::PanelRegistry& panels)
{
    if (panels.IsOpen(UI::PartyPanel))
        return false;
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    const std::int32_t width = panels.WorldWidth();
    const ActorState& hero = world.Actors()[runtime.hero];
    const auto heroClass = static_cast<std::uint8_t>(hero.characterClass & 7u);
    const float left = F32(static_cast<double>(width) - ListFromTheRight);
    const auto mouseX = static_cast<double>(context.mouse.x);
    const auto mouseY = static_cast<double>(context.mouse.y);
    float top = ListTop;
    bool picked = false;
    for (std::size_t at = 0; at < Members(runtime); ++at)
    {
        PartySlot& slot = runtime.social.party.slots[at];
        if (slot.actorIndex < 0)
        {
            if (slot.actorIndex == NotInView)
                top = F32(static_cast<double>(top) + RowHeight);
            slot.actorIndex = NotSearched;
            continue;
        }
        if (heroClass == Elf || heroClass == DarkWizard)
        {
            std::uint8_t skill = 0;
            {
                // 0x5e41d7..0x5e4448: el registro propio abierto para leer el byte de la skill del lugar de +0x425.
                const Render::Models::LogicalAccess own(context.ownRecordDepth, context.random,
                                                        Render::Models::OwnRecordCloseRandoms);
                skill = OwnSkillByte(world.OwnCharacter(), hero.skillIndex);
            }
            const auto x = static_cast<double>(left);
            const auto y = static_cast<double>(top);
            if (std::ranges::find(ListSkills, skill) != ListSkills.end() && x - HoverLeft <= mouseX &&
                mouseX <= x + HoverRight && y <= mouseY && mouseY <= y + RowHeight)
            {
                picked = true;
                context.targets[ChosenTarget] = slot.actorIndex;
            }
        }
        top = F32(static_cast<double>(top) + RowHeight);
        slot.actorIndex = NotSearched;
    }
    return picked;
}
} // namespace Client::Scenes
