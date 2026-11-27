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
std::span<Kernel::ActorState, ActorPoolSize> GameContext::Actors()
{
    return *m_actors;
}

std::span<const Kernel::ActorState, ActorPoolSize> GameContext::Actors() const
{
    return *m_actors;
}

Kernel::ActorState& GameContext::Hero()
{
    return Actors()[HeroIndex()];
}

const Kernel::ActorState& GameContext::Hero() const
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

Kernel::OwnCharacterRecord& GameContext::OwnCharacter()
{
    return m_own;
}

const Kernel::OwnCharacterRecord& GameContext::OwnCharacter() const
{
    return m_own;
}

Kernel::OwnInventory& GameContext::Inventory()
{
    return m_inventory;
}

const Kernel::OwnInventory& GameContext::Inventory() const
{
    return m_inventory;
}

Kernel::WorldMap& GameContext::Map()
{
    return m_map;
}

const Kernel::WorldMap& GameContext::Map() const
{
    return m_map;
}

Kernel::WorldRuntime& GameContext::Runtime()
{
    return m_runtime;
}

const Kernel::WorldRuntime& GameContext::Runtime() const
{
    return m_runtime;
}
} // namespace Client::Kernel
