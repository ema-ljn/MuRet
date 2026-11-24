#pragma once

#include "Core/Expected.h"

#include <array>
#include <cstdint>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}

// La interfaz de batalla del mapa 30: el mismo estado recibe B6/BB/B2 1D y alimenta paso y dibujo.
namespace Client::Scenes
{
class WorldState;
struct WorldRuntime;
struct CastleReceiveContext;
struct GameUiContext;
struct GameHudContext;

// 0x412140: conserva el objeto si el puesto sigue pidiendo la misma clase.
[[nodiscard]] Core::Expected<void, std::string> RefreshTheSiegePanel(WorldRuntime& runtime);
// 0x4102b0/0x410580: tabla fija de siete marcas; fuera de ella el original pisa el heap.
[[nodiscard]] Core::Expected<void, std::string> ReceiveSiegePanelStatus(WorldRuntime& runtime,
                                                                        const std::array<std::uint8_t, 4>& status);
// 0x411ed0: añade, no reemplaza; el índice ya viene ajustado por el caller BB.
void AppendSiegePanelMember(WorldRuntime& runtime, std::uint8_t index, std::uint8_t x, std::uint8_t y);
// 0x4e7390 y 0x413c60: reset parcial y hora/minuto sin normalizar.
void ResetSiegeClock(WorldRuntime& runtime);
void SetSiegeClock(WorldRuntime& runtime, std::uint8_t hours, std::uint8_t minutes, std::uint32_t tick);
// 0x44f120: apertura sin envío; el cierre sí usa el cable de las ventanas.
void OpenTheSiegeGate(WorldState& world, std::uint16_t key);
// 0x44dd10: primero suelta el arma actual; después los otros paneles y por último abre el modo recibido.
[[nodiscard]] Core::Expected<void, std::string> ApplySiegeWeaponOpen(const CastleReceiveContext& context,
                                                                     std::uint16_t key, std::uint8_t mode);

[[nodiscard]] Core::Expected<void, std::string> StepTheSiegeControls(const GameUiContext& context);
[[nodiscard]] Core::Expected<void, std::string> StepTheBattleSkill(const GameUiContext& context);
[[nodiscard]] Core::Expected<void, std::string> StepTheBattleSkillChoice(const GameUiContext& context);
void InitializeTheBattleSkill(WorldState& world, Crt::CrtRandom& random);
[[nodiscard]] Core::Expected<void, std::string> StepTheBattlePanel(const GameUiContext& context);
[[nodiscard]] Core::Expected<void, std::string> DrawTheSiegeControls(GameHudContext& context);
[[nodiscard]] Core::Expected<void, std::string> DrawTheBattleSkill(GameHudContext& context);
[[nodiscard]] Core::Expected<void, std::string> DrawTheBattlePanel(GameHudContext& context);
void DrawTheBattleClock(GameHudContext& context);
} // namespace Client::Scenes
