#include "World/OriginalFtol.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>

namespace Client::World
{
std::optional<std::int32_t> OriginalFtolLow(double value)
{
    // Q-C186: FISTP m64 con la operación inválida enmascarada guarda 0x8000000000000000, de dword bajo 0.
    if (std::isnan(value))
    {
        return 0;
    }
    constexpr double Int64Limit = 0x1p63;
    if (std::isinf(value) || value < -Int64Limit || value >= Int64Limit)
    {
        return std::nullopt;
    }
    const auto converted = static_cast<std::int64_t>(value);
    const auto low = static_cast<std::uint32_t>(converted);
    return std::bit_cast<std::int32_t>(low);
}
} // namespace Client::World
