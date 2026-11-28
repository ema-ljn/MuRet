#pragma once
#include "Core/Expected.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
namespace Client::Kernel
{
struct ModelEffectArguments;
} // namespace Client::Kernel

namespace Client::Effects
{
class ModelEffects;
} // namespace Client::Effects

namespace Client::CastleSiege
{
[[nodiscard]] bool IsCastleGateEffect(std::int16_t type);
// 0x518e59 → 0x51c05e: types 0x3d..0x40, only life 2 besides the prefix 0x518cf0; no RNG.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateCastleGateEffect(const Kernel::ModelEffectArguments& arguments, Effects::ModelEffects& effects);
} // namespace Client::CastleSiege
