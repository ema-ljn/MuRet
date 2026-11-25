#pragma once

#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>

// 0x69fd3c, the original's __ftol: FISTP m64 with the invalid operation masked; its callers keep the signed low dword.
// The x87 control and status words, traps and NaN payloads are not reproduced.
namespace Client::World
{
// NaN gives 0, as 0x606660 did with Devias's NaN y (Q-C186, observed). Infinity and values outside int64 give nullopt:
// what the native code does with them was never observed.
[[nodiscard]] inline std::optional<std::int32_t> OriginalFtolLow(double value)
{
    if (std::isnan(value))
    {
        return 0;
    }
    constexpr double Int64Limit = 0x1p63;
    if (std::isinf(value) || value < -Int64Limit || value >= Int64Limit)
    {
        return std::nullopt;
    }
    const auto whole = static_cast<std::int64_t>(value);
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(whole));
}

// The masked indefinite integer 0x8000000000000000 has a low dword of 0.
[[nodiscard]] inline std::int32_t FtolLow(double value)
{
    return OriginalFtolLow(value).value_or(0);
}
} // namespace Client::World
