#pragma once

#include "Actors/Drawing/CharacterDrawHooks.h"
#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Client::Kernel
{
struct JointState;
} // namespace Client::Kernel

namespace Client::Monsters
{
struct MonstersState;
} // namespace Client::Monsters

namespace Client::Render
{
class ModelBatch;
} // namespace Client::Render

namespace Client::Models
{
class IModelLibrary;
} // namespace Client::Models

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Kernel
{
enum class JointOwnerKind : std::uint8_t;
struct JointRequest;
class WorldState;
} // namespace Client::Kernel

// The characters' draw slots (Actors/Drawing/CharacterDrawHooks.h), each one in the module that owns the call. They
// keep only where the monsters' state is (ModuleStates::monsters): whoever builds a draw pass can own one.
namespace Client::Session
{
class CharacterDrawHooks final : public Actors::Drawing::ICharacterDrawHooks
{
public:
    // nullptr: the phoenix's matrices (0x50c177) stop the draw on an original frame.
    explicit CharacterDrawHooks(Monsters::MonstersState* monsters = nullptr) : m_monsters(monsters) {}

    [[nodiscard]] Core::Expected<void, std::string>
    DrawTheMonster(Render::ModelBatch& batch, const Models::IModelLibrary& models, Kernel::WorldState& world,
                   std::size_t slot, const Actors::Drawing::GameCharacterPass& pass, bool originalFrame) const override;
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
    CreateTheJoint(std::span<Kernel::JointState> joints, const Kernel::JointRequest& request, Crt::CrtRandom& random,
                   Kernel::JointOwnerKind owner) const override;

private:
    Monsters::MonstersState* m_monsters = nullptr;
};
} // namespace Client::Session
