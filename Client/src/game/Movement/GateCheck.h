#pragma once

#include "Tables/GateTable.h"
#include "Tables/GateRecord.h"

#include <cstddef>

// The pure decision of 0x599ef0 (06 § Los cambios de mapa). The caller takes the state and applies messages, wait and
// send; the protected accesses and their RNG stay outside this function.
namespace Client::Movement
{
struct GateCheckState;
struct GateDecision;

// At most one request per step. Keeps all earlier warnings in Gate.bmd order.
[[nodiscard]] GateDecision GateCheck(const Tables::GateTable& table, const GateCheckState& state);

// The same decision for one record: the integrator interleaves logical accesses and their effects between gates
// without copying 0x599ef0's equipment, level or message rules.
[[nodiscard]] GateDecision CheckGate(std::size_t index, const Tables::GateRecord& gate, const GateCheckState& state);
} // namespace Client::Movement
