#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// Efectos ordinarios de la decisión pura de gates, para que el caller los aplique en su paso.
namespace Client::GameLogic
{
struct GateWarning
{
    std::uint16_t textId = 0;
    std::optional<std::uint16_t> requiredLevel;

    friend bool operator==(const GateWarning&, const GateWarning&) = default;
};

struct GateDecision
{
    std::optional<std::uint8_t> request;
    // Sin espera, el barrido continúa: los avisos pueden preceder a un pedido u otra espera.
    std::vector<GateWarning> warnings;
    std::uint8_t waitSteps = 0;
    // 0x59abdc/0x59abfa ejecutaron el store de 0 en 0x77f3bb0, aun si ya era cero. Aplicar antes
    // del pedido, si lo hay: ese pedido vuelve a poner pending en 1.
    bool clearWarpPending = false;
};
} // namespace Client::GameLogic
