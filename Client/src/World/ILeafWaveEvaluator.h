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

// Frontera numérica de 530490/69fd3c (wave-order-v1). Evaluate retiene el intermedio, sin spill f32
// entre callbacks, hasta Truncate, que aplica el contrato 69fd3c y entrega los 32 bits bajos:
// First=FSIN(T*f32@6b89c8); Second=FSIN(T*f32@6b8a68+50f). El binding externo debe acreditar FSIN
// y el perfil x87 (PC/RC/excepciones). La interfaz no acredita std::sin ni fabrica ondas; usa scratch
// temporal prestado por Core, sin reloj, RNG ni estado persistente de clima propios.
class ILeafWaveEvaluator
{
public:
    virtual ~ILeafWaveEvaluator() = default;
    [[nodiscard]] virtual Core::Expected<void, std::error_code> Evaluate(LeafWave wave, float time) = 0;
    [[nodiscard]] virtual Core::Expected<std::int32_t, std::error_code> Truncate() = 0;
};
} // namespace Client::World
