#pragma once

#include "Data/Tables/GateTable.h"

// La decisión pura de 0x599ef0 (06 § Los cambios de mapa). El caller toma el estado y aplica
// mensajes, espera y envío; los accesos protegidos y su RNG quedan fuera de esta función.
namespace Client::GameLogic
{
struct GateCheckState;
struct GateDecision;

// A lo sumo un pedido por paso. Conserva todos los avisos previos en el orden de Gate.bmd.
[[nodiscard]] GateDecision GateCheck(const Data::Tables::GateTable& table, const GateCheckState& state);
} // namespace Client::GameLogic
