#include "Scenes/WorldState.h"

#include "Core/Expected.h"
#include "Core/IMillisecondClock32.h"
#include "Core/MillisecondTime.h"
#include <glm/ext/vector_float3.hpp>

#include "GameLogic/CharacterAppearance.h"
#include "Protocol/Character/CharacterList.h"
#include "Scenes/ActorState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldResources.h"
#include "World/ItemState.h"
#include "Scenes/WorldRuntime.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <system_error>

namespace Client::Scenes
{
std::span<World::ItemState, 64> WorldState::Inventory()
{
    return m_inventory;
}
std::span<World::ItemState, 32> WorldState::SecondaryInventory()
{
    return m_secondaryInventory;
}

WorldState::WorldState() : m_actors(std::make_unique<std::array<ActorState, ActorWindowCount>>()) {}
WorldState::WorldState(Render::ITextureStore& textures)
    : m_actors(std::make_unique<std::array<ActorState, ActorWindowCount>>()),
      m_resources(std::make_unique<WorldResources>(textures))
{
}
WorldState::~WorldState() = default;
WorldRuntime& WorldState::Runtime()
{
    return m_runtime;
}
const WorldRuntime& WorldState::Runtime() const
{
    return m_runtime;
}
Core::Expected<void, std::error_code> WorldState::SampleWorldTime(Core::IMillisecondClock32& clock)
{
    const auto sample = clock.Sample();
    if (!sample.HasValue())
        return Core::Unexpected(sample.Error());
    m_runtime.worldTime = Core::Milliseconds32ToFloatRn(sample.Value());
    return {};
}
WorldResources* WorldState::Resources()
{
    return m_resources.get();
}
const WorldResources* WorldState::Resources() const
{
    return m_resources.get();
}
std::span<ActorState, ActorWindowCount> WorldState::Actors()
{
    return *m_actors;
}
std::span<const ActorState, ActorWindowCount> WorldState::Actors() const
{
    return *m_actors;
}
OwnCharacterRecord& WorldState::OwnCharacter()
{
    return m_own;
}
const OwnCharacterRecord& WorldState::OwnCharacter() const
{
    return m_own;
}
void WorldState::SelectActor(std::optional<std::size_t> index)
{
    m_selected = index;
}
std::optional<std::size_t> WorldState::SelectedActor() const
{
    return m_selected;
}
void WorldState::ReleaseActorResources()
{
    for (ActorState& actor : Actors().first<ActorTraversalCount>())
    {
        actor.cloth = {};
    }
}

bool WorldState::PrepareSelectedActor()
{
    if (!m_selected || *m_selected >= ActorWindowCount || !(*m_actors)[*m_selected].active)
    {
        return false;
    }
    m_own.PrepareFrom((*m_actors)[*m_selected]);
    // 6342c0: el reset pertenece a esta transición, no a cada Enter de3.
    m_runtime.loadingOnceFlag = 0;
    return true;
}
ActorState& WorldState::ReconstructSelectionActor(const Protocol::Character::CharacterEntry& entry,
                                                  const glm::vec3& position)
{
    ActorState& actor = (*m_actors)[entry.slot];
    actor.position = position;
    actor.entry = entry;
    std::ranges::copy(entry.name, actor.name.begin());
    actor.name.back() = std::byte{0};
    actor.level = entry.level;
    const std::uint8_t characterClass = Protocol::Character::CharacterClassOf(entry.classByte);
    actor.appearance = entry.withoutEquipment ? GameLogic::ClassAppearance(characterClass, 0)
                                              : GameLogic::AppearanceOf(characterClass, 0, entry.equipment);
    // 511290 consulta 318 previo antes de este reemplazo; el caller ya calculó la altura.
    constexpr std::uint16_t HeightOffsetPet = 0x1c15;
    actor.pet.model = actor.appearance.pet.value_or(0xffff);
    actor.previousHad1c15 = actor.pet.model == HeightOffsetPet;
    actor.animation = {};
    actor.cloth = {};
    actor.visualRevision = 0;
    ++actor.revision;
    actor.active = true;
    return actor;
}
} // namespace Client::Scenes
