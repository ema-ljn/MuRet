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

// El lugar 2 que 0x6349a0 construye con 0x517270 y equipa antes de crear su cuervo.
namespace Client::Scenes
{

inline constexpr std::size_t LoginActorSlot = 2;

// Adaptador de presentación para tests de UI/modelos sin WorldState ni tablas del terreno.
// No simula 0x511290 ni sus rand(): main siempre usa ConstructLoginActor.
void PrepareLoginPresentationActor(Kernel::ActorState& actor, const Models::Skeleton& skeleton);

[[nodiscard]] Core::Expected<void, std::string>
ConstructLoginActor(Kernel::ActorState& actor, const Actors::ActorConstructionContext& construction,
                    Kernel::WorldRuntime& runtime, Models::ClothWorld& cloth, const Models::Skeleton& skeleton);
} // namespace Client::Scenes
