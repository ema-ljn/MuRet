#pragma once

#include <bit>

// The fractions the original keeps as floats. An AsDouble name is that float as its x87 code loads it: widened.
namespace Client::Math
{
inline constexpr float Half = std::bit_cast<float>(0x3f000000u);
inline constexpr float Third = std::bit_cast<float>(0x3eaaaaabu);
inline constexpr float Quarter = std::bit_cast<float>(0x3e800000u);
inline constexpr float Fifth = std::bit_cast<float>(0x3e4ccccdu);
inline constexpr float Eighth = std::bit_cast<float>(0x3e000000u);
inline constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu);
inline constexpr float Twentieth = std::bit_cast<float>(0x3d4ccccdu);
inline constexpr float Fiftieth = std::bit_cast<float>(0x3ca3d70au);
inline constexpr float Hundredth = std::bit_cast<float>(0x3c23d70au);
inline constexpr float Thousandth = std::bit_cast<float>(0x3a83126fu);

inline constexpr double HalfAsDouble = static_cast<double>(Half);
inline constexpr double ThirdAsDouble = static_cast<double>(Third);
inline constexpr double QuarterAsDouble = static_cast<double>(Quarter);
inline constexpr double FifthAsDouble = static_cast<double>(Fifth);
inline constexpr double EighthAsDouble = static_cast<double>(Eighth);
inline constexpr double TenthAsDouble = static_cast<double>(Tenth);
inline constexpr double TwentiethAsDouble = static_cast<double>(Twentieth);
inline constexpr double FiftiethAsDouble = static_cast<double>(Fiftieth);
inline constexpr double HundredthAsDouble = static_cast<double>(Hundredth);
inline constexpr double ThousandthAsDouble = static_cast<double>(Thousandth);
} // namespace Client::Math
