#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

// El alta de un monstruo o un NPC sobre el pool (CL9 hito 2): 0x5141e0(tipo, X, Y, clave) con los pasos de
// Scenes::MonsterKinds y su cola 0x5140c0. Contrato: Client/docs/research/06-render-escenas-y-ui.md, "El alta de un
// monstruo". *Estático*.
namespace Client::Models
{
struct Skeleton;
}

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::Monsters
{

// Los bytes de texto desde +0x1d9: el nombre (diez bytes), +0x1e3 y +0x1e4..+0x1f1 (ActorState::nameSpill).
inline constexpr std::size_t ActorTextSize = 25;
// Los que MuRet guarda seguidos desde +0x1d9: los de texto, el WORD de +0x1f2 y la clave de +0x1f4. +0x1f6 no lo
// guarda.
inline constexpr std::size_t ActorTextReadable = 29;
// Hasta +0x218, las partes del cuerpo.
inline constexpr std::size_t ActorTextReach = 0x218 - 0x1d9;

// Escribe `bytes` desde +0x1d9 + `offset` (el strcpy o el strcat sin tope del original, con su 0 incluido, Q-C847):
// después de los 25 de texto siguen el WORD de +0x1f2, la clave de +0x1f4, +0x1f6 y +0x1f7, la cabeza de +0x1f8 y el
// resto de su parte hasta +0x217, como en el original; lo que cae en +0x1f6, +0x1f7 y +0x1fa..+0x217, que ActorState
// no tiene, se pierde (P-C352). Lo que llegaría a las partes de +0x218 corta con `address`, sin escribir nada.
[[nodiscard]] Core::Expected<void, std::string> PutActorText(Kernel::ActorState& actor,
                                                             std::span<const std::byte> bytes, std::uint32_t address,
                                                             std::size_t offset = 0);

// La cadena de C desde +0x1d9, sin el 0: hasta el primer 0 de los ActorTextReadable bytes que MuRet guarda seguidos,
// o esos si no hay (el original seguiría en +0x1f6).
[[nodiscard]] std::string ActorText(const Kernel::ActorState& actor);

// El nombre de +0x1d9 como lo lee una función de cadenas del original (strcat, wsprintfA): ActorText si tiene su 0;
// sin él, el original seguiría en +0x1f6, que MuRet no guarda, y corta con `address` (Q-C847, P-C352).
[[nodiscard]] Core::Expected<std::string, std::string> ActorCString(const Kernel::ActorState& actor,
                                                                    std::uint32_t address);

// 0x5141e0 con la cola 0x5140c0: el creador de evento o el caso del switch (MonsterCreationOf con el mapa de
// WorldRuntime::map) corre sus pasos en orden, con cada rand() en su lugar entre los del alta (0x511290 y sus celdas):
// - LoadMonster y LoadNpc cargan el modelo en Assets::AssetsState::fauna (0x612d90 y 0x612270, con las texturas de la
//   tabla que liga el juego): sin Monster{n + 1}.bmd, el original cierra (0x612dd6..0x612de1, Q-C23) y MuRet corta.
//   LoadMonster escribe la tabla de sonidos del modelo también en WorldRuntime::modelSounds (MonsterSounds.h).
// - Create es CreateActor con la clave, el modelo, la celda y el ángulo del paso; Write, Name, Dress (0x512930), Box
//   (0x5133b0), Ground, Lower y CopyHeight escriben el personaje; MeshFlag y ModelByte quedan en
//   WorldRuntime::modelPatches; BoneName en WorldRuntime::namedBones (0x401000); Sound en WorldRuntime::lastSoundId
//   (CL10).
// - Las escrituras dependientes del asedio usan4294b0. Los Joint618/2,3 usan ActorState como dueño en todos los
//   casos nativos; las otras familias Joint y los Arrival aún cortan con su dirección. El Action (0x4ea030) usa
//   las acciones del modelo cargado.
// Después, la cola (MonsterTailOf): el nivel y la escala en los mapas 10 a 17, el nombre del tipo en
// WorldRuntime::npcNames (0x7681eb8) a +0x1d9 si hay uno (PutActorText, Q-C847), +0x376 con
// el tipo, +8 en 0, +0x3a4 con WorldRuntime::heroSlot y la clase del objeto. Devuelve el lugar del pool: con los 400
// vivos, el 400, que el original escribe igual.
[[nodiscard]] Core::Expected<std::size_t, std::string> CreateMonster(std::int32_t type, std::uint8_t x, std::uint8_t y,
                                                                     std::int32_t key,
                                                                     const Scenes::WorldEntryResources& resources);
} // namespace Client::Monsters
