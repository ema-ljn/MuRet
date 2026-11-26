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

// 0x4c20a0/0x4c2290 → 0x517270 y el equipo de 0x5139f0. Contrato: 06, "Los personajes de la lista".
namespace Client::Scenes
{
struct ActorConstructionContext;
class WorldState;

[[nodiscard]] Core::Expected<void, std::string> ConstructSelectionActor(WorldState& world,
                                                                        const Protocol::FrontEnd::CharacterEntry& entry,
                                                                        const ActorConstructionContext& construction,
                                                                        Models::ClothWorld& cloth,
                                                                        const Models::Skeleton& skeleton);
} // namespace Client::Scenes
