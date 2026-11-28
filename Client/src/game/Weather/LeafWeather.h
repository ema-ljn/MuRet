#pragma once

#include "Core/Expected.h"
#include "Protocol/Weather/Weather.h"

#include <cstdint>
#include <system_error>

// Persistent state of 0x530490; the caller keeps the instance between steps.
namespace Client::Weather
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

// 0x530490: T1/E1/H1/T2/E2/W/H2/V/Q. Rereads time after H1; does not change target/current. Mandatory evaluator:
// nullptr returns function_not_supported without stores. Errors propagate intact, keeping only the executed prefix
// of stores.
[[nodiscard]] Core::Expected<void, std::error_code> StepLeafWeatherWaves(LeafWeather& weather, const float& time,
                                                                         ILeafWaveEvaluator* evaluator);

// 0x4e36c4 and 0x4e36ed write only the target: 0 with a high nibble of 0, six times the low nibble with a high nibble
// of 1. The default of 0x4e5b4a writes nothing and draws no rand().
void ApplyWeather(const Protocol::Weather::Weather& received, LeafWeather& weather);
} // namespace Client::Weather
