#pragma once

#include "Core/Expected.h"
#include "Ui/OldWindows.h"
#include "Ui/PanelRegistry.h"

#include <cstdint>
#include <string>

namespace Client::Kernel
{
struct WorldRuntime;
class WorldState;
struct ActorState;
} // namespace Client::Kernel

namespace Client::Movement
{
struct HeroMovementContext;

// What the hero's step (MoveHero, 0x59adf0) asks of other modules, one slot per call of the original, in the order the
// step makes them. Session::HeroMovementHooks fills the slots.
class IHeroMovementHooks
{
public:
    virtual ~IHeroMovementHooks() = default;

    // 0x59ae24: 0x4033d0, Crywolf's camera tour.
    [[nodiscard]] virtual bool CameraTourRunning(const Kernel::WorldRuntime& runtime) const = 0;

    // 0x59afdf..0x59b033: the old windows' globals for 0x4682a0, the friend box EDIT's focus.
    [[nodiscard]] virtual Ui::OldWindowGlobals OldWindowGlobalsOf(Kernel::WorldRuntime& runtime) const = 0;

    // 0x59b188: the world's width, with its queries (two rand() with the inventory open).
    [[nodiscard]] virtual std::int32_t WorldWidth(const HeroMovementContext& context) const = 0;

    // The panels' IsOpen: 0x59b357, 0x59da75..0x59daa9, 0x56acf7 and 0x56b397..0x56b4d6. Some draw a rand().
    [[nodiscard]] virtual bool IsPanelOpen(const HeroMovementContext& context, Ui::PanelId panel) const = 0;

    // 0x56b397..0x56b4d6: closing a panel the walk closes, with the session's and the NPC windows' sends.
    [[nodiscard]] virtual Core::Expected<void, std::string> ClosePanel(const HeroMovementContext& context,
                                                                       Ui::PanelId panel) const = 0;

    // 0x56b6a9..0x56b72a: the events' window, with the serial number entry open, closes it.
    virtual void CloseTheSerialEntry(Kernel::WorldRuntime& runtime) const = 0;

    // 0x558bb0: whether the chosen character is attacked.
    [[nodiscard]] virtual Core::Expected<bool, std::string> IsAttackable(const Kernel::WorldState& world,
                                                                         bool controlDown) const = 0;

    // 0x5650c0's case 2 (0x5674c8): talking to the chosen NPC.
    [[nodiscard]] virtual Core::Expected<void, std::string> TalkToTheNpc(Kernel::ActorState& hero,
                                                                         const HeroMovementContext& context) const = 0;

    // 0x5650c0's other cases: the hit and the rest.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    InteractOnArrival(Kernel::ActorState& hero, const HeroMovementContext& context) const = 0;

    // 0x59e255: 0x57cfc0, the hero's skill.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    UseTheHeroSkill(Kernel::ActorState& hero, const HeroMovementContext& context) const = 0;
};
} // namespace Client::Movement
