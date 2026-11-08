#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <system_error>

namespace Client::World
{
enum class LeafWave : std::uint8_t
{
    First,
    Second,
};

// Frontera numérica de 530490/69fd3c (wave-order-v1). Evaluate retiene el intermedio
// hasta Truncate, que entrega los 32 bits bajos. El binding externo debe acreditar
// FSIN y el perfil x87 (PC/RC/excepciones), sin introducir un spill f32 entre callbacks.
// La interfaz no acredita std::sin ni aporta reloj, RNG o estado persistente de clima.
class ILeafWaveEvaluator
{
public:
    virtual ~ILeafWaveEvaluator() = default;
    [[nodiscard]] virtual Core::Expected<void, std::error_code> Evaluate(LeafWave wave, float time) = 0;
    [[nodiscard]] virtual Core::Expected<std::int32_t, std::error_code> Truncate() = 0;
};
} // namespace Client::World
