#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <system_error>

// Estado persistente de 0x530490; Core conserva la instancia entre mapas.
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

// Prefijo de 0x530490, antes de las ondas: el mapa se captura una vez; el selector solo afecta a 34
// (predicado puro 0x4303c0). No limpia las hojas que quedan fuera del prefijo devuelto.
[[nodiscard]] std::size_t PrepareLeafWeather(LeafWeather& weather, std::int32_t map, std::int32_t selector);

// Orden de las dos ondas y stores de530490; time se relee después de la primera conversión.
// Sin evaluator devuelve function_not_supported; los errores conservan el prefijo ya ejecutado.
// No incluye PrepareLeafWeather ni visita slots. El evaluador productivo sigue siendo de Core.
[[nodiscard]] Core::Expected<void, std::error_code> StepLeafWeatherWaves(LeafWeather& weather, const float& time,
                                                                         ILeafWaveEvaluator* evaluator);

// Después de las dos conversiones de onda y antes de visitar el pool: suma modular32, resto signed.
void AdvanceLeafWeatherPhase(LeafWeather& weather);
} // namespace Client::World
