#pragma once

#include "EngineApi.h"

// Correctly rounded sin, cos, tan, acos and log10: each returns the float or double nearest to the exact value,
// so every compiler, system and CPU gives the same bits. The client calls these, never the C library's.
namespace Client::Math
{
[[nodiscard]] ENGINE_API float RoundedSin(float x);
[[nodiscard]] ENGINE_API double RoundedSin(double x);
[[nodiscard]] ENGINE_API float RoundedCos(float x);
[[nodiscard]] ENGINE_API double RoundedCos(double x);
[[nodiscard]] ENGINE_API float RoundedTan(float x);
[[nodiscard]] ENGINE_API double RoundedTan(double x);
[[nodiscard]] ENGINE_API float RoundedAcos(float x);
[[nodiscard]] ENGINE_API double RoundedAcos(double x);
[[nodiscard]] ENGINE_API double RoundedLog10(double x);

// Any other argument type would convert in silence.
template <typename T> void RoundedSin(T) = delete;
template <typename T> void RoundedCos(T) = delete;
template <typename T> void RoundedTan(T) = delete;
template <typename T> void RoundedAcos(T) = delete;
template <typename T> void RoundedLog10(T) = delete;
} // namespace Client::Math
