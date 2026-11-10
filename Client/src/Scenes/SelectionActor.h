#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Protocol::Character
{
struct CharacterEntry;
}

namespace Client::Render::Models
{
struct ClothWorld;
struct Skeleton;
} // namespace Client::Render::Models

// 0x4c20a0/0x4c2290 → 0x517270 y el equipo de 0x5139f0. Contrato: 06, "Los personajes de la lista".
namespace Client::Scenes
{
struct ActorConstructionContext;
class WorldState;

[[nodiscard]] Core::Expected<void, std::string>
ConstructSelectionActor(WorldState& world, const Protocol::Character::CharacterEntry& entry,
                        const ActorConstructionContext& construction, Render::Models::ClothWorld& cloth,
                        const Render::Models::Skeleton& skeleton);
} // namespace Client::Scenes
