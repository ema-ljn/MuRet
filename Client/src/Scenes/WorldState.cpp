#include "Scenes/WorldState.h"

#include "Scenes/ActorState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"

#include <span>

namespace Client::Scenes
{
std::span<ActorState, ActorPoolSize> WorldState::Actors()
{
    return m_actors;
}

std::span<const ActorState, ActorPoolSize> WorldState::Actors() const
{
    return m_actors;
}

OwnCharacterRecord& WorldState::OwnCharacter()
{
    return m_own;
}

const OwnCharacterRecord& WorldState::OwnCharacter() const
{
    return m_own;
}

WorldRuntime& WorldState::Runtime()
{
    return m_runtime;
}

const WorldRuntime& WorldState::Runtime() const
{
    return m_runtime;
}
} // namespace Client::Scenes
