#include "Scenes/ActionSend.h"

#include "Core/Expected.h"
#include "World/OriginalFtol.h"

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
// 0x6b87dc (22,5f), 0x6b87d8 (0,0222f, 1/45 redondeado a float) y 0x6b87d4 (1,0f).
constexpr float HalfSector = 22.5f;
constexpr float SectorReciprocal = 0.02222222276031971f;
constexpr double SectorBias = 1.0;
// 0x576b5e..0x576b6e: AND 0xff y el resto por 8 con signo.
constexpr std::uint32_t LowByte = 0xff;
constexpr std::uint32_t DirectionMask = 7;
} // namespace

std::optional<std::uint8_t> ActionDirection(float angle)
{
    const double sector = ((static_cast<double>(angle) + HalfSector) * SectorReciprocal) + SectorBias;
    const auto whole = World::OriginalFtolLow(sector);
    if (!whole)
        return std::nullopt;
    const std::uint32_t low = static_cast<std::uint32_t>(*whole) & LowByte;
    return static_cast<std::uint8_t>(low & DirectionMask);
}

Core::Expected<void, std::string> SendTheAction(float angle, std::uint8_t action, const ActionSender& send,
                                                std::string_view where)
{
    const auto direction = ActionDirection(angle);
    if (!direction)
        return Core::Unexpected(std::format("{}: el ángulo del C1 18 está fuera del _ftol (0x69fd3c)", where));
    if (!send)
        return Core::Unexpected(std::format("{}: no hay quien mande el C1 18 (la acción {:#x})", where, action));
    send(*direction, action);
    return {};
}
} // namespace Client::Scenes
