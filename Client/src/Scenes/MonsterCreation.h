#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

// El alta de un monstruo o un NPC sobre el pool (CL9 hito 2): 0x5141e0(tipo, X, Y, clave) con los pasos de
// Scenes::MonsterKinds y su cola 0x5140c0. Contrato: Client/docs/research/06-render-escenas-y-ui.md, "El alta de un
// monstruo". *Estático*.
namespace Client::Render::Models
{
struct Skeleton;
}

namespace Client::Scenes
{
struct ActorState;
struct WorldEntryResources;

// El esqueleto del modelo `model` como lo lee 0x4ea030 (0x5769ad8 + modelo · 0xe0): primero los que cargan 0x612d90 y
// 0x612270 (WorldState::FaunaResources, que sigue con la biblioteca del mapa) y después los de `resources.models`.
[[nodiscard]] const Render::Models::Skeleton* MonsterSkeletonOf(const WorldEntryResources& resources,
                                                                std::uint16_t model);

// 0x4ea030(personaje, acción) con el esqueleto de MonsterSkeletonOf; un modelo que no está corta con `address`.
[[nodiscard]] Core::Expected<void, std::string>
SetMonsterAction(ActorState& actor, std::uint8_t action, std::uint32_t address, const WorldEntryResources& resources);

// Lo que MuRet guarda desde +0x1d9: el nombre (diez bytes), +0x1e3 y +0x1e4..+0x1f1 (ActorState::nameSpill).
inline constexpr std::size_t ActorTextSize = 25;

// Escribe `bytes` desde +0x1d9 + `offset` (el strcpy o el strcat del original, con su 0 incluido). Lo que llegaría a
// +0x1f2 (el WORD de +0x1f2 y la clave de +0x1f4, Q-C847) corta con `address`, sin escribir nada.
[[nodiscard]] Core::Expected<void, std::string> PutActorText(ActorState& actor, std::span<const std::byte> bytes,
                                                             std::uint32_t address, std::size_t offset = 0);

// La cadena de C desde +0x1d9, sin el 0: hasta el primer 0 de los 25 bytes de MuRet, o los 25 si no hay.
[[nodiscard]] std::string ActorText(const ActorState& actor);

// 0x5141e0 con la cola 0x5140c0: el creador de evento o el caso del switch (MonsterCreationOf con el mapa de
// WorldRuntime::map) corre sus pasos en orden, con cada rand() en su lugar entre los del alta (0x511290 y sus celdas):
// - LoadMonster y LoadNpc cargan el modelo en WorldState::FaunaResources (0x612d90 y 0x612270, con las texturas de la
//   tabla que liga el juego): sin Monster{n + 1}.bmd, el original cierra (0x612dd6..0x612de1, Q-C23) y MuRet corta.
//   LoadMonster escribe la tabla de sonidos del modelo también en WorldRuntime::modelSounds (MonsterSounds.h).
// - Create es CreateActor con la clave, el modelo, la celda y el ángulo del paso; Write, Name, Dress (0x512930), Box
//   (0x5133b0), Ground, Lower y CopyHeight escriben el personaje; MeshFlag y ModelByte quedan en
//   WorldRuntime::modelPatches; BoneName en WorldRuntime::namedBones (0x401000); Sound en WorldRuntime::lastSoundId
//   (CL10).
// - Cortan, con su dirección, lo de las otras CL: el Siege con el asedio prendido (0x4294b0, CL9.5), los Joint
//   (0x530860) y los Arrival (0x518cf0) de la CL11. El Action (0x4ea030) usa las acciones del modelo cargado.
// Después, la cola (MonsterTailOf): el nivel y la escala en los mapas 10 a 17, +0x376 con el tipo, +8 en 0, +0x3a4 con
// WorldRuntime::heroSlot y la clase del objeto. El nombre de la tabla de 0x7681eb8 no está (08, P-C174). Devuelve el
// lugar del pool: con los 400 vivos, el 400, que el original escribe igual.
[[nodiscard]] Core::Expected<std::size_t, std::string> CreateMonster(std::int32_t type, std::uint8_t x, std::uint8_t y,
                                                                     std::int32_t key,
                                                                     const WorldEntryResources& resources);
} // namespace Client::Scenes
