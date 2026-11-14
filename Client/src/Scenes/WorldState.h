#pragma once

#include "Protocol/Character/CharacterList.h"
#include "Scenes/ActorState.h"
#include "Scenes/DynamicWorldLight.h"
#include "Scenes/HelperTable.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/PlayerActionSpeeds.h"
#include "Scenes/FaunaState.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/GameDrawTail.h"
#include "World/Leaves.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <memory>

namespace Client::Render::Models
{
class CrtRandom;
}

// Lo que WinMain deja en cero y dura de una escena a otra: el pool de 401 registros de personaje
// (REP STOSD en 0x4bafa6..0x4bafb3, con el héroe en el primero, 0x4bafdf), el registro propio
// ([0x7681eb0], 0x4bafbc..0x4bafc2), las grillas del inventario propio (0x78a9bb0 y 0x78ab0b8, en la
// .bss), los 10 ayudantes de 0x7d335d0 (también en la .bss: HelperTable.h) y los globales en claro
// (WorldRuntime). Además, lo que 0x550d10 deja en el objeto de los conjuntos ([0x6eed3c], el estático
// de 0x6e5930), que arranca en cero y no lo vuelve a vaciar nadie. main es su único dueño y lo presta:
// no se copia ni se mueve, para que no haya otra copia de la historia.
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Los personajes de la lista".
namespace Client::Scenes
{
inline constexpr std::size_t ActorPoolSize = 401;
// 0x4c20a0 pone cada personaje de la lista en chars[lugar], con el lugar de 0 a 4.
inline constexpr std::size_t SelectionActorCount = 5;

// +0x68 y +0x6c..+0x74 de un modelo compartido (0x5769ad8 + tipo · 0xe0): la escala y la posición del último
// objeto de ese tipo que las copió. 0x4ec110 con Translate en 1 las suma a los huesos que le pasan, aunque
// sean de otro objeto del mismo tipo.
struct SharedModelPlacement
{
    float scale = 0.0f;
    glm::vec3 origin{};
};

// 0x6b88d4 (0,01f): con menos alpha, 0x6001b0 sale sin copiar nada al modelo.
inline constexpr float PlayerModelAlpha = 0.01f;

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
    // Lo que llena el F3 10 fuera del equipo (06, "El F3 10, el inventario").
    [[nodiscard]] OwnInventory& Inventory();
    [[nodiscard]] const OwnInventory& Inventory() const;
    [[nodiscard]] WorldRuntime& Runtime();
    [[nodiscard]] const WorldRuntime& Runtime() const;
    // Lo que la última carga de mapa (0x61a560) dejó en la CPU.
    [[nodiscard]] WorldMap& Map();
    [[nodiscard]] const WorldMap& Map() const;
    [[nodiscard]] ItemSetState& ItemSets();
    [[nodiscard]] const ItemSetState& ItemSets() const;
    // Los ayudantes del juego (0x7d335d0): el caballo oscuro de cada jinete.
    [[nodiscard]] HelperTable& Helpers();
    [[nodiscard]] const HelperTable& Helpers() const;
    // 0x743a198 y su viento: un pool para login, selección y juego, en el heap por su tamaño.
    [[nodiscard]] World::Particles& Particles();
    [[nodiscard]] const World::Particles& Particles() const;
    // 0x7b4ccc4 y los límites del último 0x5fbb00: una historia para las tres escenas.
    [[nodiscard]] DynamicWorldLight& DynamicLighting();
    [[nodiscard]] const DynamicWorldLight& DynamicLighting() const;
    [[nodiscard]] PlayerActionSpeeds& ActionSpeeds();
    [[nodiscard]] const PlayerActionSpeeds& ActionSpeeds() const;
    [[nodiscard]] FaunaState& Fauna();
    [[nodiscard]] const FaunaState& Fauna() const;
    [[nodiscard]] FaunaModels& FaunaResources();
    [[nodiscard]] const FaunaModels& FaunaResources() const;
    [[nodiscard]] FaunaDrawCarry& PoseCarry();
    [[nodiscard]] const FaunaDrawCarry& PoseCarry() const;
    [[nodiscard]] GameDrawTailState& DrawTail();
    [[nodiscard]] const GameDrawTailState& DrawTail() const;
    [[nodiscard]] World::Leaves& Leaves();
    [[nodiscard]] const World::Leaves& Leaves() const;
    // Player.bmd (0x20a), compartido por todos los jugadores: lo copian el paso de cada uno (0x505ec6) y la
    // pose de su cuerpo (0x6001b0, con el alpha desde 0,01), en el juego, el login y la selección. Lo lee el
    // cuervo en la zona segura (0x4178d2 y 0x4179a5), antes del 0x505ec6 de su dueño (Q-C412). Arranca en
    // cero: antes de la primera copia, MuRet suma nada.
    [[nodiscard]] SharedModelPlacement& PlayerModel();
    [[nodiscard]] const SharedModelPlacement& PlayerModel() const;
    // Lo que 0x505ec6 y 0x6001b0 copian de `actor` si es un 0x20a; con otro tipo, a otro modelo.
    void PlacePlayerModel(const ActorState& actor);
    // 0x505ec6..0x505ed8, en el paso de cada personaje: la escala y la posición de `actor` al modelo de su tipo (el
    // 0x20a, a PlayerModel). Las lee 0x52e810, los restos del que termina de morir, desde el paso de su cuerpo, que va
    // antes: las del último de ese tipo que pasó (CL9 hito 3, Q-C1178).
    void PlaceModel(const ActorState& actor);
    // La escala y la posición que tiene el modelo `type` (0x5769ad8 + tipo · 0xe0, +0x68 y +0x6c). En cero hasta la
    // primera copia, como el de Player.bmd. El dibujo de los monstruos (0x6001b0, sin portar, P-C241 g) no las escribe.
    [[nodiscard]] SharedModelPlacement ModelPlacement(std::uint16_t type) const;

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
    OwnInventory m_inventory;
    WorldRuntime m_runtime;
    WorldMap m_map;
    ItemSetState m_itemSets;
    HelperTable m_helpers{};
    std::unique_ptr<World::Particles> m_particles = std::make_unique<World::Particles>();
    DynamicWorldLight m_dynamicLighting;
    PlayerActionSpeeds m_actionSpeeds;
    FaunaState m_fauna;
    FaunaModels m_faunaResources;
    FaunaDrawCarry m_poseCarry;
    std::unique_ptr<GameDrawTailState> m_drawTail = std::make_unique<GameDrawTailState>();
    std::unique_ptr<World::Leaves> m_leaves = std::make_unique<World::Leaves>();
    SharedModelPlacement m_playerModel;
    std::map<std::uint16_t, SharedModelPlacement> m_modelPlacements;
};
} // namespace Client::Scenes
