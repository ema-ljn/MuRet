#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <system_error>

// Estado persistente de 530490; el llamador conserva la instancia entre pasos.
namespace Client::World
{
class ILeafWaveEvaluator;

struct LeafWeather
{
    std::int32_t target = 0;     // 0x75fdc38
    std::int32_t current = 0;    // 0x75fdc3c
    std::int32_t firstWave = 0;  // 0x6c8c5c
    std::int32_t secondWave = 0; // 0x75fdc40
    std::int32_t phase = 0;      // 0x75fdc44
};

// 530490: T1/E1/H1/T2/E2/W/H2/V/Q. Relee time tras H1; no cambia target/current.
// Evaluador obligatorio: nullptr devuelve function_not_supported sin stores.
// Los errores se propagan intactos, conservando sólo el prefijo de stores ejecutado.
[[nodiscard]] Core::Expected<void, std::error_code> StepLeafWeatherWaves(LeafWeather& weather, const float& time,
                                                                         ILeafWaveEvaluator* evaluator);
} // namespace Client::World
