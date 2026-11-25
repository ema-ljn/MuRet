#pragma once

#include <bit>
#include <cstdint>

// Turns in degrees, in each type the original computes them with, and its two factors from degrees to radians.
namespace Client::Math
{
inline constexpr std::int32_t FullTurn = 360;

inline constexpr float FullTurnAsFloat = 360.0f;
inline constexpr float HalfTurnAsFloat = 180.0f;
inline constexpr float QuarterTurnAsFloat = 90.0f;

inline constexpr double FullTurnAsDouble = 360.0;
inline constexpr double HalfTurnAsDouble = 180.0;
inline constexpr double QuarterTurnAsDouble = 90.0;

// The float nearest to π/180 and the next float above it.
inline constexpr float DegreesToRadians = std::bit_cast<float>(0x3c8efa35u);
inline constexpr float DegreesToRadiansRoundedUp = std::bit_cast<float>(0x3c8efa36u);
inline constexpr double DegreesToRadiansAsDouble = static_cast<double>(DegreesToRadians);
} // namespace Client::Math
