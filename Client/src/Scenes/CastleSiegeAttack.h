#pragma once
#include "Core/Expected.h"
#include <cstdint>
#include <functional>
#include <string>
namespace Client::Crt
{
class CrtRandom;
}
namespace Client::World
{
struct ModelEffectArguments;
}
namespace Client::Scenes
{
struct ActorState;
using CastleAttackEmitter = std::function<Core::Expected<void, std::string>(const World::ModelEffectArguments&)>;
// 42ea00: caller sobre el blanco del ataque; el caller exterior sólo lo invoca enmapa30.
[[nodiscard]] Core::Expected<void, std::string> EmitTheCastleAttackHit(const ActorState& target,
                                                                       std::uint32_t heroFlags, Crt::CrtRandom& random,
                                                                       const CastleAttackEmitter& emit);
} // namespace Client::Scenes
