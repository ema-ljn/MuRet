#pragma once

#include "Kernel/ActorState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"

#include <array>
#include <cstddef>
#include <memory>
#include <span>

// The state every module reads: the actor pool and the hero in it, the player's own record and inventory, what the
// last map load left and the world's runtime. Scenes::WorldState forwards to it while AR3 and AR4 move the rest.
namespace Client::Kernel
{
inline constexpr std::size_t ActorPoolSize = 401;

class GameContext
{
public:
    GameContext() = default;
    ~GameContext() = default;
    GameContext(const GameContext&) = delete;
    GameContext& operator=(const GameContext&) = delete;
    GameContext(GameContext&&) = delete;
    GameContext& operator=(GameContext&&) = delete;

    [[nodiscard]] std::span<Scenes::ActorState, ActorPoolSize> Actors();
    [[nodiscard]] std::span<const Scenes::ActorState, ActorPoolSize> Actors() const;
    // Actors()[HeroIndex()], with no check, as the original reads it: actor 0 until the F3 03 names the hero.
    [[nodiscard]] Scenes::ActorState& Hero();
    [[nodiscard]] const Scenes::ActorState& Hero() const;
    [[nodiscard]] std::size_t HeroIndex() const;
    void SetHero(std::size_t index);
    [[nodiscard]] Scenes::OwnCharacterRecord& OwnCharacter();
    [[nodiscard]] const Scenes::OwnCharacterRecord& OwnCharacter() const;
    [[nodiscard]] Scenes::OwnInventory& Inventory();
    [[nodiscard]] const Scenes::OwnInventory& Inventory() const;
    [[nodiscard]] Scenes::WorldMap& Map();
    [[nodiscard]] const Scenes::WorldMap& Map() const;
    // Until AR4 empties it.
    [[nodiscard]] Scenes::WorldRuntime& Runtime();
    [[nodiscard]] const Scenes::WorldRuntime& Runtime() const;

private:
    // On the heap: with the pool inside, a local WorldState (main.cpp, the tests) passed 900 KB and overflowed the
    // 1 MB stack of MSVC's Debug on Windows.
    std::unique_ptr<std::array<Scenes::ActorState, ActorPoolSize>> m_actors =
        std::make_unique<std::array<Scenes::ActorState, ActorPoolSize>>();
    Scenes::OwnCharacterRecord m_own;
    Scenes::OwnInventory m_inventory;
    Scenes::WorldRuntime m_runtime;
    Scenes::WorldMap m_map;
};
} // namespace Client::Kernel
