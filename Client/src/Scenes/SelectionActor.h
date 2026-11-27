#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Protocol::FrontEnd
{
struct CharacterEntry;
}

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
class WorldState;
} // namespace Client::Kernel

// 0x4c20a0/0x4c2290 → 0x517270 y el equipo de 0x5139f0. Contrato: 06, "Los personajes de la lista".
namespace Client::Scenes
{

[[nodiscard]] Core::Expected<void, std::string>
ConstructSelectionActor(Kernel::WorldState& world, const Protocol::FrontEnd::CharacterEntry& entry,
                        const Actors::ActorConstructionContext& construction, Models::ClothWorld& cloth,
                        const Models::Skeleton& skeleton);
} // namespace Client::Scenes
