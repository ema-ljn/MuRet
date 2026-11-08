#include "World/OriginalFtol.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>

namespace Client::World
{
std::optional<std::int32_t> OriginalFtolLow(double value)
{
    constexpr double Int64Limit = 0x1p63;
    if (!std::isfinite(value) || value < -Int64Limit || value >= Int64Limit)
    {
        return std::nullopt;
    }
    const auto converted = static_cast<std::int64_t>(value);
    const auto low = static_cast<std::uint32_t>(converted);
    return std::bit_cast<std::int32_t>(low);
}
} // namespace Client::World
