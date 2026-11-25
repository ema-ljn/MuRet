#pragma once

#include "Core/Expected.h"
#include "Tables/GateTable.h"
#include "GameLogic/GateDecision.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}

// 0x599ef0: barrido de Gate.bmd con los accesos lógicos, efectos y envío en orden (06, Pisar un gate).
namespace Client::Scenes
{
class WorldState;
struct GameGateContext
{
    WorldState& world;
    const Data::Tables::GateTable& gates;
    Crt::CrtRandom& random;
    std::uint32_t ticksMs = 0;
    std::function<void(std::uint8_t)> requestGate{};
    // 0x7879f80: sólo lo consulta Atlans con cursorItemCount>0; cero mientras no hay productor.
    std::int16_t alternateMount = 0;
    // 0x59ad3b/0x59ad45: F3 30 y Data\Macro.txt al pedir un gate con destino30/31, aún dentro del acceso propio.
    std::function<Core::Expected<void, std::string>()> saveOptionsAndMacros{};
};

// Aplica contador, pending y objetivos; el resultado conserva avisos para el futuro área de mensajes.
// No decrementa el contador: lo hizo MoveHero antes. El destino30/31 guarda opciones y macros después del envío
// y de la espera, antes de pending/objetivos. El caller decide cuándo lo invoca.
[[nodiscard]] Core::Expected<GameLogic::GateDecision, std::string> CheckGameGates(const GameGateContext& context);
} // namespace Client::Scenes
