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

// Frontera numérica de530490. Evaluate conserva el intermedio sin spillf32 hasta Truncate:
// First=FSIN(T*f32@6b89c8); Second=FSIN(T*f32@6b8a68+50f).
// Truncate aplica el contrato69fd3c y devuelve sus32 bits bajos. El binding debe acreditar su perfil
// x87 (PC/RC/excepciones); esta interfaz no afirma equivalencia con std::sin ni fabrica ondas.
// Scratch temporal prestado por Core, sin reloj propio, RNG o estado persistente de clima duplicado.
class ILeafWaveEvaluator
{
public:
    virtual ~ILeafWaveEvaluator() = default;
    [[nodiscard]] virtual Core::Expected<void, std::error_code> Evaluate(LeafWave wave, float time) = 0;
    [[nodiscard]] virtual Core::Expected<std::int32_t, std::error_code> Truncate() = 0;
};
} // namespace Client::World
