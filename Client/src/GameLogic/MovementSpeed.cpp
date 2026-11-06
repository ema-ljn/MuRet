#include "GameLogic/MovementSpeed.h"

#include <bit>
#include <cstdint>

namespace Client::GameLogic
{
namespace
{
// 06 § Caminar: discriminadores y constantes de 0x508940, sin importar la política de Walk.
constexpr std::uint16_t PlayerType = 0x020a;
constexpr std::uint8_t SpecialMovementMode = 1;
constexpr std::uint16_t SharedCounterItem = 0x1c37;
constexpr std::uint16_t RunningItem = 0x1c16;
constexpr std::int16_t FirstRunningItem = 0x1c14;
constexpr std::int16_t LastRunningItem = 0x1c15;
constexpr std::uint16_t NoWings = 0xffff;
constexpr std::uint16_t FasterWings = 0x1a17;
constexpr std::uint8_t RunningThreshold = 40;
constexpr std::int32_t FirstItemThreshold = 10;
constexpr std::int32_t SecondItemThreshold = 20;
constexpr std::int32_t FirstOverrideMap = 18;
constexpr std::int32_t LastOverrideMap = 23;
constexpr std::uint32_t HalfSpeedFlag = 0x2;
constexpr float WalkingSpeed = 12.0f;
constexpr float RunningSpeed = 15.0f;
constexpr float WingsSpeed = 16.0f;
constexpr float ItemSpeed = 17.0f;
constexpr float VariantSpeed = 19.0f;
constexpr float HalfSpeedFactor = 0.5f;

float SharedItemSpeed(MovementSpeedSharedState& shared, std::uint8_t variant)
{
    if (shared.itemLatch == 0)
    {
        // 0x508996, luego 0x50899c: conservar el orden de los dos stores persistentes.
        shared.itemLatch = 1;
        shared.itemCounterBits = 0;
        return RunningSpeed;
    }
    const auto counter = std::bit_cast<std::int32_t>(shared.itemCounterBits);
    if (counter < FirstItemThreshold)
    {
        return RunningSpeed;
    }
    if (counter < SecondItemThreshold)
    {
        return WingsSpeed;
    }
    return variant == 0 ? ItemSpeed : VariantSpeed;
}

float SpecialMovementSpeed(std::uint8_t& run, MovementSpeedSharedState& shared, const MovementSpeedInput& input)
{
    if (input.item == SharedCounterItem && input.safeZone == 0)
    {
        // La ruta de 17 comparte la constante con 0x508a1f, pero evita el store de Run.
        return SharedItemSpeed(shared, input.itemVariant);
    }
    if (input.item == RunningItem && input.safeZone == 0)
    {
        run = RunningThreshold;
        return ItemSpeed;
    }
    const auto signedItem = std::bit_cast<std::int16_t>(input.item);
    const bool runningItem = signedItem >= FirstRunningItem && signedItem <= LastRunningItem;
    if ((input.wings != NoWings || runningItem) && input.safeZone == 0)
    {
        run = RunningThreshold;
        return input.wings == FasterWings ? WingsSpeed : RunningSpeed;
    }
    return run < RunningThreshold ? WalkingSpeed : RunningSpeed;
}
} // namespace

float UpdateMovementSpeed(std::uint8_t& run, MovementSpeedSharedState& shared, const MovementSpeedInput& input)
{
    auto speed = static_cast<float>(input.baseSpeed);
    if (input.type == PlayerType && input.movementMode == SpecialMovementMode)
    {
        speed = SpecialMovementSpeed(run, shared, input);
    }
    // El mapa se aplica después de los efectos de ítems, también para tipos no jugadores.
    if (input.map >= FirstOverrideMap && input.map <= LastOverrideMap)
    {
        run = RunningThreshold;
        speed = RunningSpeed;
    }
    return (input.flags & HalfSpeedFlag) != 0 ? speed * HalfSpeedFactor : speed;
}
} // namespace Client::GameLogic
