#pragma once

#include "Core/Expected.h"
#include "Protocol/Character/CharacterList.h"
#include <glm/ext/vector_float3.hpp>

#include "Scenes/ActorState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "World/ItemState.h"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <system_error>

namespace Client::Core
{
class IMillisecondClock32;
}

namespace Client::Render
{
class ITextureStore;
}

namespace Client::Scenes
{
// 4b9b80: ventana inicializada de 401; cleanup y sorteo recorren sólo 400.
inline constexpr std::size_t ActorWindowCount = 401;
inline constexpr std::size_t ActorTraversalCount = 400;
inline constexpr std::size_t SelectionActorCount = 5;
inline constexpr std::size_t LoginActorSlot = 2;

struct WorldResources;

class WorldState
{
public:
    WorldState();
    explicit WorldState(Render::ITextureStore& textures);
    ~WorldState();
    [[nodiscard]] WorldRuntime& Runtime();
    [[nodiscard]] const WorldRuntime& Runtime() const;
    // 4eb620: una muestra qT independiente; publica F32 RN sólo si Sample tuvo éxito.
    [[nodiscard]] Core::Expected<void, std::error_code> SampleWorldTime(Core::IMillisecondClock32& clock);
    [[nodiscard]] WorldResources* Resources();
    [[nodiscard]] const WorldResources* Resources() const;
    WorldState(const WorldState&) = delete;
    WorldState& operator=(const WorldState&) = delete;
    WorldState(WorldState&&) = delete;
    WorldState& operator=(WorldState&&) = delete;
    [[nodiscard]] std::span<ActorState, ActorWindowCount> Actors();
    [[nodiscard]] std::span<const ActorState, ActorWindowCount> Actors() const;
    [[nodiscard]] std::span<World::ItemState, 64> Inventory();
    [[nodiscard]] std::span<World::ItemState, 32> SecondaryInventory();
    [[nodiscard]] OwnCharacterRecord& OwnCharacter();
    [[nodiscard]] const OwnCharacterRecord& OwnCharacter() const;
    // Índice de selección 6d5edc, producido por la UI (0..4), no H sorteado del juego.
    // nullopt significa ningún elegido; los constructores de actor no lo reemplazan.
    void SelectActor(std::optional<std::size_t> index);
    [[nodiscard]] std::optional<std::size_t> SelectedActor() const;
    [[nodiscard]] bool PrepareSelectedActor();
    // Proyección cloth de cleanup510f70: recorre 400, conserva XYZ/318 y el registro adicional.
    void ReleaseActorResources();
    // Precondición: entry.slot pertenece a 0..4, filtrado por ServerSession antes del evento.
    ActorState& ReconstructSelectionActor(const Protocol::Character::CharacterEntry& entry, const glm::vec3& position);

private:
    std::unique_ptr<std::array<ActorState, ActorWindowCount>> m_actors;
    OwnCharacterRecord m_own;
    std::array<World::ItemState, 64> m_inventory{};
    std::array<World::ItemState, 32> m_secondaryInventory{};
    WorldRuntime m_runtime;
    std::unique_ptr<WorldResources> m_resources;
    std::optional<std::size_t> m_selected;
};
} // namespace Client::Scenes
