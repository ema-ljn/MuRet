#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <string>

namespace Client::Render::Models
{
struct ClothWorld;
struct Skeleton;
} // namespace Client::Render::Models

// El lugar 2 que 0x6349a0 construye con 0x517270 y equipa antes de crear su cuervo.
namespace Client::Scenes
{
struct ActorState;
struct ActorConstructionContext;
struct WorldRuntime;

inline constexpr std::size_t LoginActorSlot = 2;

// Adaptador de presentación para tests de UI/modelos sin WorldState ni tablas del terreno.
// No simula 0x511290 ni sus rand(): main siempre usa ConstructLoginActor.
void PrepareLoginPresentationActor(ActorState& actor, const Render::Models::Skeleton& skeleton);

[[nodiscard]] Core::Expected<void, std::string>
ConstructLoginActor(ActorState& actor, const ActorConstructionContext& construction, WorldRuntime& runtime,
                    Render::Models::ClothWorld& cloth, const Render::Models::Skeleton& skeleton);
} // namespace Client::Scenes
