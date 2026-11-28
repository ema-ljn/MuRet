#include "Weather/LeafWeather.h"

#include "Core/Expected.h"
#include "Protocol/Weather/Weather.h"
#include "Weather/ILeafWaveEvaluator.h"

#include <bit>
#include <cstdint>
#include <system_error>

namespace Client::Weather
{
namespace
{
// 0x530490, frozen contract wave-order-v1: arithmetic of integer stores, not of the evaluator.
constexpr std::uint32_t FirstWaveOffset = 3;
constexpr std::uint32_t FirstWaveScale = 10;
constexpr std::uint32_t SecondWaveScale = 20;
constexpr std::uint32_t PhaseStep = 20;
constexpr std::int32_t PhasePeriod = 2000;

constexpr std::uint8_t WeatherModeShift = 4;
constexpr std::uint8_t WeatherIntensityMask = 0x0f;
constexpr std::int32_t TargetPerIntensity = 6;

void AdvancePhase(LeafWeather& weather)
{
    // 0x530564..0x530585: 32-bit ADD followed by IDIV with a signed remainder.
    const std::uint32_t sum = static_cast<std::uint32_t>(weather.phase) + PhaseStep;
    weather.phase = std::bit_cast<std::int32_t>(sum) % PhasePeriod;
}
} // namespace

Core::Expected<void, std::error_code> StepLeafWeatherWaves(LeafWeather& weather, const float& time,
                                                           ILeafWaveEvaluator* evaluator)
{
    if (evaluator == nullptr)
    {
        return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
    }
    const auto firstSine = evaluator->Evaluate(LeafWave::First, time);
    if (!firstSine.HasValue())
    {
        return Core::Unexpected(firstSine.Error());
    }
    const auto first = evaluator->Truncate();
    if (!first.HasValue())
    {
        return Core::Unexpected(first.Error());
    }
    // T2 is reread after H1. W must be visible in H2, but not yet in E2.
    const auto secondSine = evaluator->Evaluate(LeafWave::Second, time);
    if (!secondSine.HasValue())
    {
        return Core::Unexpected(secondSine.Error());
    }
    const std::uint32_t firstBits = (static_cast<std::uint32_t>(first.Value()) + FirstWaveOffset) * FirstWaveScale;
    weather.firstWave = std::bit_cast<std::int32_t>(firstBits);
    const auto second = evaluator->Truncate();
    if (!second.HasValue())
    {
        return Core::Unexpected(second.Error());
    }
    const std::uint32_t secondBits = static_cast<std::uint32_t>(second.Value()) * SecondWaveScale;
    weather.secondWave = std::bit_cast<std::int32_t>(secondBits);
    AdvancePhase(weather);
    return {};
}

void ApplyWeather(const Protocol::Weather::Weather& received, LeafWeather& weather)
{
    const auto mode = received.code >> WeatherModeShift;
    if (mode == 0)
        weather.target = 0;
    else if (mode == 1)
        weather.target = static_cast<std::int32_t>(received.code & WeatherIntensityMask) * TargetPerIntensity;
}
} // namespace Client::Weather
