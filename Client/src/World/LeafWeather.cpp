#include "World/LeafWeather.h"

#include "Core/Expected.h"
#include "World/ILeafWaveEvaluator.h"

#include <bit>
#include <cstdint>
#include <system_error>

namespace Client::World
{
namespace
{
// 530490, contrato congelado wave-order-v1: aritmética de stores enteros, no del evaluador.
constexpr std::uint32_t FirstWaveOffset = 3;
constexpr std::uint32_t FirstWaveScale = 10;
constexpr std::uint32_t SecondWaveScale = 20;
constexpr std::uint32_t PhaseStep = 20;
constexpr std::int32_t PhasePeriod = 2000;

void AdvancePhase(LeafWeather& weather)
{
    // 0x530564..0x530585: ADD de 32 bits seguido de IDIV con resto signed.
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
    // T2 se relee después de H1. W debe ser visible en H2, pero todavía no en E2.
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
} // namespace Client::World
