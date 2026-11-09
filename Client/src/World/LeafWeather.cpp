#include "World/LeafWeather.h"

#include "Core/Expected.h"
#include "World/ILeafWaveEvaluator.h"

#include <bit>
#include <cstddef>
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
constexpr std::int32_t ForcedTarget = 100;
constexpr std::size_t OrdinaryCount = 80;
constexpr std::size_t MapNineCount = 200;
constexpr std::size_t CastleSiegeCount = 40;
constexpr std::size_t CrywolfFirstCount = 60;
constexpr std::size_t CrywolfSecondCount = 50;

std::size_t PrefixCount(std::int32_t map, std::int32_t selector)
{
    // 0x5304d5..0x53050c; 0x4303c0 solo reconoce la igualdad raw32 con 34.
    if (map == 30)
        return CastleSiegeCount;
    if (map == 34 && selector == 1)
        return CrywolfFirstCount;
    if (map == 34 && selector == 2)
        return CrywolfSecondCount;
    return map == 9 ? MapNineCount : OrdinaryCount;
}
} // namespace

std::size_t PrepareLeafWeather(LeafWeather& weather, std::int32_t map, std::int32_t selector)
{
    // 0x5304a9..0x530526: el objetivo forzado persiste al abandonar esos mapas.
    const std::size_t count = PrefixCount(map, selector);
    if (map == 10 || (map >= 18 && map <= 23))
        weather.target = ForcedTarget;
    if (weather.current > weather.target)
        --weather.current;
    else if (weather.current < weather.target)
        ++weather.current;
    return count;
}

Core::Expected<void, std::error_code> StepLeafWeatherWaves(LeafWeather& weather, const float& time,
                                                           ILeafWaveEvaluator* evaluator)
{
    if (evaluator == nullptr)
        return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
    const auto firstSine = evaluator->Evaluate(LeafWave::First, time);
    if (!firstSine.HasValue())
        return Core::Unexpected(firstSine.Error());
    const auto first = evaluator->Truncate();
    if (!first.HasValue())
        return Core::Unexpected(first.Error());
    // T2 se relee después de H1. W se escribe después de FSIN2 y antes de H2: debe ser visible en H2,
    // pero todavía no en E2.
    const auto secondSine = evaluator->Evaluate(LeafWave::Second, time);
    if (!secondSine.HasValue())
        return Core::Unexpected(secondSine.Error());
    const std::uint32_t firstBits = (static_cast<std::uint32_t>(first.Value()) + FirstWaveOffset) * FirstWaveScale;
    weather.firstWave = std::bit_cast<std::int32_t>(firstBits);
    const auto second = evaluator->Truncate();
    if (!second.HasValue())
        return Core::Unexpected(second.Error());
    const std::uint32_t secondBits = static_cast<std::uint32_t>(second.Value()) * SecondWaveScale;
    weather.secondWave = std::bit_cast<std::int32_t>(secondBits);
    AdvanceLeafWeatherPhase(weather);
    return {};
}

void AdvanceLeafWeatherPhase(LeafWeather& weather)
{
    // 0x530564..0x530585: ADD de 32 bits seguido de IDIV con resto signed; nunca módulo euclídeo ni
    // overflow signed del host. Lee la fase viva: un callback anterior pudo cambiarla.
    const std::uint32_t sum = static_cast<std::uint32_t>(weather.phase) + PhaseStep;
    weather.phase = std::bit_cast<std::int32_t>(sum) % PhasePeriod;
}
} // namespace Client::World
