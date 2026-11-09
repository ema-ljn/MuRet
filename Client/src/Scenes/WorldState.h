#pragma once

#include "Scenes/ActorState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"

#include <array>
#include <cstddef>
#include <span>

// Lo que WinMain deja en cero y dura de una escena a otra: el pool de 401 registros de personaje
// (REP STOSD en 0x4bafa6..0x4bafb3, con el héroe en el primero, 0x4bafdf), el registro propio
// ([0x7681eb0], 0x4bafbc..0x4bafc2) y los globales en claro (WorldRuntime). main es su único dueño
// y lo presta: no se copia ni se mueve, para que no haya otra copia de la historia.
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Los personajes de la lista".
namespace Client::Scenes
{
inline constexpr std::size_t ActorPoolSize = 401;
// 0x4c20a0 pone cada personaje de la lista en chars[lugar], con el lugar de 0 a 4.
inline constexpr std::size_t SelectionActorCount = 5;

class WorldState
{
public:
    WorldState() = default;
    ~WorldState() = default;
    WorldState(const WorldState&) = delete;
    WorldState& operator=(const WorldState&) = delete;
    WorldState(WorldState&&) = delete;
    WorldState& operator=(WorldState&&) = delete;

    [[nodiscard]] std::span<ActorState, ActorPoolSize> Actors();
    [[nodiscard]] std::span<const ActorState, ActorPoolSize> Actors() const;
    [[nodiscard]] OwnCharacterRecord& OwnCharacter();
    [[nodiscard]] const OwnCharacterRecord& OwnCharacter() const;
    [[nodiscard]] WorldRuntime& Runtime();
    [[nodiscard]] const WorldRuntime& Runtime() const;

private:
    std::array<ActorState, ActorPoolSize> m_actors{};
    OwnCharacterRecord m_own;
    WorldRuntime m_runtime;
};
} // namespace Client::Scenes
