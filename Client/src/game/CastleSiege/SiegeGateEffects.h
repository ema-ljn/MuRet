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

namespace Client::World
{
class ModelEffects;
[[nodiscard]] bool IsCastleGateEffect(std::int16_t type);
// 518e59→51c05e: tipos3d..40, sólo vida2 además del prefijo518cf0; ningúnRNG.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateCastleGateEffect(const Kernel::ModelEffectArguments& arguments, ModelEffects& effects);
} // namespace Client::World
