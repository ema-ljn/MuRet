#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::World
{
class ModelEffects;
struct ModelEffectArguments;

inline constexpr std::int16_t SiegeStoneEffect = 0x12b;
inline constexpr std::int16_t SiegeIceStoneEffect = 0x12c;

[[nodiscard]] bool IsSiegeStoneEffect(std::int16_t type);
// 0x51cfa4 y0x51d1f0, después del prefijo518cf0. Lleno es éxito siníndice y sinCRT.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateSiegeStone(const ModelEffectArguments& arguments, ModelEffects& effects, const glm::vec3& heroPosition,
                 Render::Models::CrtRandom& random);
} // namespace Client::World
