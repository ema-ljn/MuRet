#include "Scenes/Skill4e9920.h"

#include "Core/Expected.h"
#include "World/OriginalFtol.h"

#include <bit>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr float MinusCellReciprocal = std::bit_cast<float>(std::uint32_t{0xbc23d70a}); // [0x6ba110], −0,01

// El producto en la x87 sin redondear (exacto en double para un float o un entero de menos de 2^29) y el byte bajo del
// ftol, que es lo que suman y restan MOV BL,AL y SUB DL,BL.
std::optional<std::uint8_t> LowByteOf(double value)
{
    const auto truncated = World::OriginalFtolLow(value * static_cast<double>(MinusCellReciprocal));
    if (!truncated)
        return std::nullopt;
    return static_cast<std::uint8_t>(*truncated & 0xff);
}
} // namespace

Core::Expected<std::uint8_t, std::string> RelativeCellOf(float px, float py, std::int32_t tx, std::int32_t ty)
{
    const auto targetY = LowByteOf(static_cast<double>(ty));
    const auto heroY = LowByteOf(static_cast<double>(py));
    const auto targetX = LowByteOf(static_cast<double>(tx));
    const auto heroX = LowByteOf(static_cast<double>(px));
    if (!targetY || !heroY || !targetX || !heroX)
        return Core::Unexpected(std::string("0x4e992d: un ftol de la posición relativa fuera de su perfil"));
    const auto high = static_cast<std::uint8_t>((*heroY - *targetY + 8) << 4);
    const auto low = static_cast<std::uint8_t>(*heroX - *targetX + 8);
    return static_cast<std::uint8_t>(low | high);
}
} // namespace Client::Scenes
