#include "Scenes/Skill4e9920.h"

#include "World/OriginalFtol.h"

#include <bit>
#include <cstdint>
#include <optional>

namespace Client::Scenes
{
namespace
{
constexpr double NegativeHundredth = static_cast<double>(std::bit_cast<float>(std::uint32_t{0xbc23d70a})); // −0,01
constexpr std::uint8_t NibbleBias = 8;
constexpr unsigned HighNibble = 4;
} // namespace

std::optional<std::uint8_t> RelativeTargetCell(float x, float y, std::int32_t targetX, std::int32_t targetY)
{
    // 0x4e9923..0x4e9955: la Y del blanco primero, después la del punto; el byte de arriba.
    const auto cellTargetY = World::OriginalFtolLow(static_cast<double>(targetY) * NegativeHundredth);
    const auto cellY = World::OriginalFtolLow(static_cast<double>(y) * NegativeHundredth);
    const auto cellTargetX = World::OriginalFtolLow(static_cast<double>(targetX) * NegativeHundredth);
    const auto cellX = World::OriginalFtolLow(static_cast<double>(x) * NegativeHundredth);
    if (!cellTargetY || !cellY || !cellTargetX || !cellX)
        return std::nullopt;
    const auto low = [](std::int32_t value) { return static_cast<std::uint8_t>(static_cast<std::uint32_t>(value)); };
    const auto high = static_cast<std::uint8_t>(static_cast<std::uint8_t>(low(*cellY) - low(*cellTargetY) + NibbleBias)
                                                << HighNibble);
    const auto right = static_cast<std::uint8_t>(low(*cellX) - low(*cellTargetX) + NibbleBias);
    return static_cast<std::uint8_t>(right | high);
}
} // namespace Client::Scenes
