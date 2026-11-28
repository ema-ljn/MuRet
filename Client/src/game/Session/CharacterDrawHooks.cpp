#include "Session/CharacterDrawHooks.h"

#include "Core/Expected.h"
#include "Kernel/JointRequest.h"
#include "Scenes/FaunaJointCreation.h"
#include "Monsters/Drawing/MonsterDrawing.h"

#include <cstddef>
#include <optional>
#include <span>
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

Core::Expected<std::optional<std::size_t>, std::string>
CharacterDrawHooks::CreateTheJoint(std::span<Scenes::JointState> joints, const Kernel::JointRequest& request,
                                   Crt::CrtRandom& random, Kernel::JointOwnerKind owner) const
{
    return Scenes::CreateFaunaJoint(joints, request, random, owner);
}
} // namespace Client::Session
