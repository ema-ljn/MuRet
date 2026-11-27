#include "Session/CharacterDrawHooks.h"

#include "Core/Expected.h"
#include "Scenes/MonsterDrawing.h"

#include <cstddef>
#include <string>

namespace Client::Session
{
Core::Expected<void, std::string> CharacterDrawHooks::DrawTheMonster(Render::ModelBatch& batch,
                                                                     const Models::IModelLibrary& models,
                                                                     Kernel::WorldState& world, std::size_t slot,
                                                                     const Actors::Drawing::GameCharacterPass& pass,
                                                                     bool originalFrame) const
{
    return Scenes::DrawGameMonster(batch, models, world, slot, pass, originalFrame);
}
} // namespace Client::Session
