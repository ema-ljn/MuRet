#pragma once
#include "Core/Expected.h"
#include <cstdint>
#include <functional>
#include <string>
namespace Client::Crt
{
class CrtRandom;
}
namespace Client::Kernel
{
struct ModelEffectArguments;
struct ActorState;
} // namespace Client::Kernel

namespace Client::CastleSiege
{
using CastleAttackEmitter = std::function<Core::Expected<void, std::string>(const Kernel::ModelEffectArguments&)>;
// 42ea00: caller sobre el blanco del ataque; el caller exterior sólo lo invoca enmapa30.
[[nodiscard]] Core::Expected<void, std::string> EmitTheCastleAttackHit(const Kernel::ActorState& target,
                                                                       std::uint32_t heroFlags, Crt::CrtRandom& random,
                                                                       const CastleAttackEmitter& emit);
} // namespace Client::CastleSiege
