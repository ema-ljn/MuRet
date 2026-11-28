#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <string>

namespace Client::Models
{
struct ClothWorld;
struct Skeleton;
} // namespace Client::Models

namespace Client::Actors
{
struct ActorConstructionContext;
} // namespace Client::Actors

namespace Client::Kernel
{
struct ActorState;
struct WorldRuntime;
} // namespace Client::Kernel

// Slot 2, which 0x6349a0 builds with 0x517270 and equips before creating its raven.
namespace Client::FrontEnd
{

inline constexpr std::size_t LoginActorSlot = 2;

// Presentation adapter for UI/model tests without WorldState or terrain tables.
// It does not simulate 0x511290 or its rand(): main always uses ConstructLoginActor.
void PrepareLoginPresentationActor(Kernel::ActorState& actor, const Models::Skeleton& skeleton);

[[nodiscard]] Core::Expected<void, std::string>
ConstructLoginActor(Kernel::ActorState& actor, const Actors::ActorConstructionContext& construction,
                    Kernel::WorldRuntime& runtime, Models::ClothWorld& cloth, const Models::Skeleton& skeleton);
} // namespace Client::FrontEnd
