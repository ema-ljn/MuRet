#pragma once

#include "Core/Expected.h"
#include "Movement/HeroMovementHooks.h"
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
} // namespace Client::Movement

// The hero's step slots (Movement/HeroMovementHooks.h), each one in the module that owns the call. They keep no state:
// whoever builds a movement context can own one.
namespace Client::Session
{
class HeroMovementHooks final : public Movement::IHeroMovementHooks
{
public:
    [[nodiscard]] bool CameraTourRunning(const Kernel::WorldRuntime& runtime) const override;
    [[nodiscard]] Ui::OldWindowGlobals OldWindowGlobalsOf(Kernel::WorldRuntime& runtime) const override;
    [[nodiscard]] std::int32_t WorldWidth(const Movement::HeroMovementContext& context) const override;
    [[nodiscard]] bool IsPanelOpen(const Movement::HeroMovementContext& context, Ui::PanelId panel) const override;
    [[nodiscard]] Core::Expected<void, std::string> ClosePanel(const Movement::HeroMovementContext& context,
                                                               Ui::PanelId panel) const override;
    void CloseTheSerialEntry(Kernel::WorldRuntime& runtime) const override;
    [[nodiscard]] Core::Expected<bool, std::string> IsAttackable(const Kernel::WorldState& world,
                                                                 bool controlDown) const override;
    [[nodiscard]] Core::Expected<void, std::string>
    TalkToTheNpc(Kernel::ActorState& hero, const Movement::HeroMovementContext& context) const override;
    [[nodiscard]] Core::Expected<void, std::string>
    InteractOnArrival(Kernel::ActorState& hero, const Movement::HeroMovementContext& context) const override;
    [[nodiscard]] Core::Expected<void, std::string>
    UseTheHeroSkill(Kernel::ActorState& hero, const Movement::HeroMovementContext& context) const override;
};
} // namespace Client::Session
