#pragma once

// Correctly rounded sin, cos, tan, acos and log10: each returns the float or double nearest to the exact value,
// so every compiler, system and CPU gives the same bits. The client calls these, never the C library's.
namespace Client::Render::Models
{
[[nodiscard]] float RoundedSin(float x);
[[nodiscard]] double RoundedSin(double x);
[[nodiscard]] float RoundedCos(float x);
[[nodiscard]] double RoundedCos(double x);
[[nodiscard]] float RoundedTan(float x);
[[nodiscard]] double RoundedTan(double x);
[[nodiscard]] float RoundedAcos(float x);
[[nodiscard]] double RoundedAcos(double x);
[[nodiscard]] double RoundedLog10(double x);

// Any other argument type would convert in silence.
template <typename T> void RoundedSin(T) = delete;
template <typename T> void RoundedCos(T) = delete;
template <typename T> void RoundedTan(T) = delete;
template <typename T> void RoundedAcos(T) = delete;
template <typename T> void RoundedLog10(T) = delete;
} // namespace Client::Render::Models
