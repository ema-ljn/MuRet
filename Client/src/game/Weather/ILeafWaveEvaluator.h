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

// Frontera numérica de 530490/69fd3c (wave-order-v1). Evaluate retiene el intermedio hasta
// Truncate, que entrega los 32 bits bajos de su conversión signed64 truncada, sin spill f32 ni
// double entre los dos callbacks. Con una conversión inválida son los del entero indefinido
// enmascarado, 0x8000000000000000 (low32 = 0), como en el perfil de ADR0012 y en FtolLow.
// El binding sigue el perfil retenido de ADR0014 (FirstLeafSine, SecondLeafSine y
// TruncateLeafSine de OriginalRetainedMath): el seno del argumento R53 redondeado una sola vez a
// 64 bits, al más cercano. Es un modelo portable documentado, no el FSIN de la x87: ADR0014
// registra en qué valores difieren, y el camino con la x87 real quedó diferido el 2026-11-09.
// La interfaz no usa std::sin ni aporta reloj, RNG o estado persistente de clima.
class ILeafWaveEvaluator
{
public:
    virtual ~ILeafWaveEvaluator() = default;
    [[nodiscard]] virtual Core::Expected<void, std::error_code> Evaluate(LeafWave wave, float time) = 0;
    [[nodiscard]] virtual Core::Expected<std::int32_t, std::error_code> Truncate() = 0;
};
} // namespace Client::World
