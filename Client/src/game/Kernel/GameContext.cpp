#include "Kernel/GameContext.h"

#include "Kernel/ActorState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"

#include <cstddef>
#include <span>

namespace Client::Kernel
{
std::span<Scenes::ActorState, ActorPoolSize> GameContext::Actors()
{
    return *m_actors;
}

std::span<const Scenes::ActorState, ActorPoolSize> GameContext::Actors() const
{
    return *m_actors;
}

Scenes::ActorState& GameContext::Hero()
{
    return Actors()[HeroIndex()];
}

const Scenes::ActorState& GameContext::Hero() const
{
    return Actors()[HeroIndex()];
}

std::size_t GameContext::HeroIndex() const
{
    return m_runtime.hero;
}

void GameContext::SetHero(std::size_t index)
{
    m_runtime.hero = index;
}

Scenes::OwnCharacterRecord& GameContext::OwnCharacter()
{
    return m_own;
}

const Scenes::OwnCharacterRecord& GameContext::OwnCharacter() const
{
    return m_own;
}

Scenes::OwnInventory& GameContext::Inventory()
{
    return m_inventory;
}

const Scenes::OwnInventory& GameContext::Inventory() const
{
    return m_inventory;
}

Scenes::WorldMap& GameContext::Map()
{
    return m_map;
}

const Scenes::WorldMap& GameContext::Map() const
{
    return m_map;
}

Scenes::WorldRuntime& GameContext::Runtime()
{
    return m_runtime;
}

const Scenes::WorldRuntime& GameContext::Runtime() const
{
    return m_runtime;
}
} // namespace Client::Kernel
