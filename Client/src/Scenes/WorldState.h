#pragma once

#include "Protocol/Character/CharacterList.h"
#include "Scenes/ActorState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"

#include <array>
#include <cstddef>
#include <span>

namespace Client::Render::Models
{
class CrtRandom;
}

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

    // Lo que hace la selección al pasar a la escena 3 con `chosen`, el elegido (06, "La lógica" y "La
    // entrada al mundo", paso 1; los tres caminos: Enter 0x6343e2..0x634470, doble clic
    // 0x634546..0x634638 y el botón de 0x633c10, hasta 0x633f05): abre el registro propio, le copia
    // el nivel, la clase, la piel y el nombre, lo cierra con sus 3.848 rand() de `random` si queda sin
    // accesos (0x40f370 o 0x40b4d0) y baja loadingOnceFlag. La escena suelta lo suyo en su Leave
    // (0x61cc50).
    void PrepareSelectedActor(const Protocol::Character::CharacterEntry& chosen, Render::Models::CrtRandom& random);

private:
    std::array<ActorState, ActorPoolSize> m_actors{};
    OwnCharacterRecord m_own;
    WorldRuntime m_runtime;
};
} // namespace Client::Scenes
