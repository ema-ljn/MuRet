#pragma once

#include "Actors/JointHooks.h"
#include "Core/Expected.h"

#include <cstddef>
#include <string>

namespace Client::Render
{
class ModelBatch;
} // namespace Client::Render

namespace Client::Models
{
class IModelLibrary;
} // namespace Client::Models

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Actors::Drawing
{
struct GameCharacterPass;

// What the characters' draw (0x510d00 → 0x50ba30) asks of other modules, each call where the original makes it.
class ICharacterDrawHooks : public IJointHooks
{
public:
    virtual ~ICharacterDrawHooks() = default;

    // 0x50ba30 with a monster or an NPC (a type other than the 0x20a) in pool slot `slot`.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    DrawTheMonster(Render::ModelBatch& batch, const Models::IModelLibrary& models, Kernel::WorldState& world,
                   std::size_t slot, const GameCharacterPass& pass, bool originalFrame) const = 0;
    // Then the joints of what hangs, of the full set and of the raven (IJointHooks).
};
} // namespace Client::Actors::Drawing
