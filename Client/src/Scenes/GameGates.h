#pragma once

#include "Core/Expected.h"
#include "Data/Tables/GateTable.h"
#include "GameLogic/GateDecision.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Render::Models
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
    Render::Models::CrtRandom& random;
    std::uint32_t ticksMs = 0;
    std::function<void(std::uint8_t)> requestGate{};
    // 0x7879f80: sólo lo consulta Atlans con cursorItemCount>0; cero mientras no hay productor.
    std::int16_t alternateMount = 0;
};

// Aplica contador, pending y objetivos; el resultado conserva avisos para el futuro área de mensajes.
// No decrementa el contador: lo hizo MoveHero antes. El destino30/31 corta tras envío y espera,
// antes de las macros no portadas y antes de pending/objetivos. El caller decide cuándo lo invoca.
[[nodiscard]] Core::Expected<GameLogic::GateDecision, std::string> CheckGameGates(const GameGateContext& context);
} // namespace Client::Scenes
