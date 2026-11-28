#include "Session/HeroMovementHooks.h"

#include "Core/Expected.h"
#include "Events/Crywolf/CameraTour.h"
#include "Events/EventChipWindow.h"
#include "Combat/Attackable.h"
#include "Hero/HeroAttack.h"
#include "Hud/GamePanels.h"
#include "Hud/GameUi.h"
#include "Items/InventoryMoves.h"
#include "Kernel/ActorState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Movement/GameMovement.h"
#include "Npc/NpcTalk.h"
#include "Skills/HeroSkills.h"
#include "Ui/OldWindows.h"
#include "Ui/PanelRegistry.h"

#include <cstdint>
#include <string>
#include <utility>

namespace Client::Session
{
namespace
{
// What 0x5650c0 needs from MoveHero (0x59b2d2 and 0x59d670): the player's skeleton goes apart, and the base models stay
// for the actions of transformed heroes.
Hero::HeroArrivalContext ArrivalContextOf(const Movement::HeroMovementContext& context)
{
    return Hero::HeroArrivalContext{.combat = {.world = context.world,
                                               .random = context.random,
                                               .playerSkeleton = context.playerSkeleton,
                                               .models = context.models,
                                               .items = context.items,
                                               .terrain = context.terrain,
                                               .dynamicLighting = context.dynamicLighting,
                                               .faunaModels = context.faunaModels},
                                    .sendWalk = context.sendWalk,
                                    .sendAttack = context.sendAttack,
                                    .skills = context.skills,
                                    .tickCount = [ticks = context.input.ticksMs] { return ticks; },
                                    .inventory = context.inventory,
                                    .controlDown = context.input.controlDown,
                                    .sendSkill = context.sendSkill};
}
} // namespace

bool HeroMovementHooks::CameraTourRunning(const Kernel::WorldRuntime& runtime) const
{
    return Events::Crywolf::CameraTourRunning(runtime.events.cameraTour);
}

Ui::OldWindowGlobals HeroMovementHooks::OldWindowGlobalsOf(Kernel::WorldRuntime& runtime) const
{
    return Hud::OldWindowGlobalsOf(runtime);
}

std::int32_t HeroMovementHooks::WorldWidth(const Movement::HeroMovementContext& context) const
{
    Hud::GamePanels panels(context.world, context.random);
    return panels.Registry().WorldWidth();
}

bool HeroMovementHooks::IsPanelOpen(const Movement::HeroMovementContext& context, Ui::PanelId panel) const
{
    Hud::GamePanels panels(context.world, context.random);
    return panels.Registry().IsOpen(panel);
}

Core::Expected<void, std::string> HeroMovementHooks::ClosePanel(const Movement::HeroMovementContext& context,
                                                                Ui::PanelId panel) const
{
    Hud::GamePanels panels(context.world, context.random);
    panels.LendTheSession(context.world, context.random, context.sendGamePacket, context.sendWindowClose);
    // 0x56b3d2, 0x56b41e and 0x56b48a: the warehouse, the chaos box and the NPC shop close through 0x657ab0, which
    // empties their grids and sends their packet (Ui::PanelContext::npcWindowClosing).
    panels.LendTheNpcWindows(context.world, context.random,
                             context.inventory != nullptr ? *context.inventory : Items::InventoryServices{},
                             context.sendNpcWindow);
    if (auto closed = panels.Registry().Close(panel); !closed.HasValue())
        return Core::Unexpected(std::move(closed).Error());
    return {};
}

void HeroMovementHooks::CloseTheSerialEntry(Kernel::WorldRuntime& runtime) const
{
    if (runtime.panels.field78abb00 == Events::SerialNumberWindow)
        Events::CloseTheSerialEntry(runtime);
}

Core::Expected<bool, std::string> HeroMovementHooks::IsAttackable(const Kernel::WorldState& world,
                                                                  bool controlDown) const
{
    return Combat::IsAttackable(world, controlDown);
}

Core::Expected<void, std::string> HeroMovementHooks::TalkToTheNpc(Kernel::ActorState& hero,
                                                                  const Movement::HeroMovementContext& context) const
{
    return Npc::TalkToTheNpc(hero, context);
}

Core::Expected<void, std::string>
HeroMovementHooks::InteractOnArrival(Kernel::ActorState& hero, const Movement::HeroMovementContext& context) const
{
    return Hero::InteractOnArrival(hero, ArrivalContextOf(context));
}

Core::Expected<void, std::string> HeroMovementHooks::UseTheHeroSkill(Kernel::ActorState& hero,
                                                                     const Movement::HeroMovementContext& context) const
{
    return Skills::UseTheHeroSkill(
        hero, Skills::HeroSkillContext{.world = context.world,
                                       .random = context.random,
                                       .skills = context.skills,
                                       .playerSkeleton = context.playerSkeleton,
                                       .items = context.items,
                                       .models = context.models,
                                       .tickCount = [ticks = context.input.ticksMs] { return ticks; },
                                       .inventory = context.inventory,
                                       .worldTime = context.input.worldTime,
                                       .controlDown = context.input.controlDown,
                                       .pickTerrainPoint = context.pickTerrainPoint,
                                       .send = context.sendSkill,
                                       .playerKey = context.playerKey,
                                       .terrain = context.terrain,
                                       .dynamicLighting = context.dynamicLighting,
                                       .faunaModels = context.faunaModels});
}
} // namespace Client::Session
