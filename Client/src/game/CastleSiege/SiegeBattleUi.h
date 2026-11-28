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

// La interfaz de batalla del mapa 30: el mismo estado recibe B6/BB/B2 1D y alimenta paso y dibujo.
namespace Client::CastleSiege
{
struct CastleReceiveContext;

// 0x412140: conserva el objeto si el puesto sigue pidiendo la misma clase.
[[nodiscard]] Core::Expected<void, std::string> RefreshTheSiegePanel(Kernel::WorldRuntime& runtime);
// 0x4102b0/0x410580: tabla fija de siete marcas; fuera de ella el original pisa el heap.
[[nodiscard]] Core::Expected<void, std::string> ReceiveSiegePanelStatus(Kernel::WorldRuntime& runtime,
                                                                        const std::array<std::uint8_t, 4>& status);
// 0x411ed0: añade, no reemplaza; el índice ya viene ajustado por el caller BB.
void AppendSiegePanelMember(Kernel::WorldRuntime& runtime, std::uint8_t index, std::uint8_t x, std::uint8_t y);
// 0x4e7390 y 0x413c60: reset parcial y hora/minuto sin normalizar.
void ResetSiegeClock(Kernel::WorldRuntime& runtime);
void SetSiegeClock(Kernel::WorldRuntime& runtime, std::uint8_t hours, std::uint8_t minutes, std::uint32_t tick);
// 0x44f120: apertura sin envío; el cierre sí usa el cable de las ventanas.
void OpenTheSiegeGate(Kernel::WorldState& world, std::uint16_t key);
// 0x44dd10: primero suelta el arma actual; después los otros paneles y por último abre el modo recibido.
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
