#pragma once

#include "Core/Expected.h"

#include <array>
#include <cstdint>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
class WorldState;
struct WorldRuntime;
} // namespace Client::Kernel

namespace Client::Hud
{
struct GameUiContext;
struct GameHudContext;
} // namespace Client::Hud

// Map 30's battle interface: the same state receives B6/BB/B2 1D and feeds the step and the draw.
namespace Client::CastleSiege
{
struct CastleReceiveContext;

// 0x412140: keeps the object if the post still asks for the same class.
[[nodiscard]] Core::Expected<void, std::string> RefreshTheSiegePanel(Kernel::WorldRuntime& runtime);
// 0x4102b0/0x410580: a fixed table of seven marks; outside it the original overwrites the heap.
[[nodiscard]] Core::Expected<void, std::string> ReceiveSiegePanelStatus(Kernel::WorldRuntime& runtime,
                                                                        const std::array<std::uint8_t, 4>& status);
// 0x411ed0: appends, does not replace; the index comes already adjusted by the BB caller.
void AppendSiegePanelMember(Kernel::WorldRuntime& runtime, std::uint8_t index, std::uint8_t x, std::uint8_t y);
// 0x4e7390 and 0x413c60: partial reset and hour/minute without normalizing.
void ResetSiegeClock(Kernel::WorldRuntime& runtime);
void SetSiegeClock(Kernel::WorldRuntime& runtime, std::uint8_t hours, std::uint8_t minutes, std::uint32_t tick);
// 0x44f120: opening without a send; the close does use the windows' wire.
void OpenTheSiegeGate(Kernel::WorldState& world, std::uint16_t key);
// 0x44dd10: first releases the current weapon; then the other panels and finally opens the received mode.
[[nodiscard]] Core::Expected<void, std::string> ApplySiegeWeaponOpen(const CastleReceiveContext& context,
                                                                     std::uint16_t key, std::uint8_t mode);

[[nodiscard]] Core::Expected<void, std::string> StepTheSiegeControls(const Hud::GameUiContext& context);
[[nodiscard]] Core::Expected<void, std::string> StepTheBattleSkill(const Hud::GameUiContext& context);
[[nodiscard]] Core::Expected<void, std::string> StepTheBattleSkillChoice(const Hud::GameUiContext& context);
void InitializeTheBattleSkill(Kernel::WorldState& world, Crt::CrtRandom& random);
[[nodiscard]] Core::Expected<void, std::string> StepTheBattlePanel(const Hud::GameUiContext& context);
[[nodiscard]] Core::Expected<void, std::string> DrawTheSiegeControls(Hud::GameHudContext& context);
[[nodiscard]] Core::Expected<void, std::string> DrawTheBattleSkill(Hud::GameHudContext& context);
[[nodiscard]] Core::Expected<void, std::string> DrawTheBattlePanel(Hud::GameHudContext& context);
void DrawTheBattleClock(Hud::GameHudContext& context);
} // namespace Client::CastleSiege
