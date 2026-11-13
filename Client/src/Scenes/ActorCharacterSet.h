#pragma once

#include "Core/Expected.h"
#include "GameLogic/CharacterAppearance.h"

#include <cstddef>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}

// El equipo de otro personaje desde los 17 bytes de la lista (0x5139f0), en el juego: el 0x12 (0x4c91fa) y, cuando se
// porte, el F3 13 (0x4e316e, InventoryMoves.h). Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Los otros
// jugadores en el viewport". Todo *estático*, del ASM (0x5139f0..0x5140b3).
namespace Client::Scenes
{
class WorldState;
struct WorldEntryResources;

// 0x513b3c..0x513ced con el cuarto argumento en 0: la mascota de b[4] & 3 (0x1c12 + el valor con su ayudante: 0x1c12
// con el 0, ninguno con el 1, 0xc6 con el 2; con el 3, 0x1c15 y el 0x110 si b[9] & 1, o sin mascota con +0x31b y
// +0x31c en 0), después el caballo oscuro de b[11] & 1 (0x1c16 y el 0xbf) y Fenrir de b[11] & 4 (0x1c37, +0x31b en
// b[15] & 3 y el ayudante 0x14a con 1, 0x14c con 2 y 0x14b con los demás), cada uno con 0x443fa0 en ese orden. La
// selección (SelectionActor) y el juego lo comparten.
[[nodiscard]] Core::Expected<void, std::string> CreateCharacterSetPets(WorldState& world, std::size_t slot,
                                                                       GameLogic::Equipment equipment,
                                                                       Render::Models::CrtRandom& random);

// 0x5139f0(index, equipo, 0, 0) en el juego. `index` es lo que dio 0x5110d0: el 400 (el registro 401 del pool, que
// existe) si no hay ninguno. Sin el tipo 0x20a en +2, nada. Si no, en este orden: las manos (el tipo de 12 bits,
// ((b[11 o 12] & 0xf0) << 4) | b[0 o 1], más 0x212; el vacío 0x1fff no se da nunca: Q-C102, y MuRet toma el 0xfff como
// sin ítem, como en la selección); con la clase 4 y el tipo 0xa05 en la izquierda, el cuervo 0x1c17 y 0x420cf0; las
// alas; 0x443d40 (los ayudantes del personaje, apagados); la mascota (CreateCharacterSetPets); 0x414340; +0x360 con
// b[11] & 2 (4 si no estaba entre 1 y 3; sin el bit, 0 si no estaba entre 1 y 3); el nivel de las manos (los bits 0..2
// y 3..5 de b[5..7] por 0x513340) y +0x2fa y +0x31a en 0. Con +0x3e3 en 0, además: las cinco partes del cuerpo (el
// índice 0x1ff es la de la clase) con sus niveles, los bits de excelente (b[9]) y de set (b[10]) de cada parte y de
// las manos, +0x358 con b[10] & 1, 0x4053f0 y 0x512930. Lo que MuRet no tiene corta con error después de lo de antes.
[[nodiscard]] Core::Expected<void, std::string> ApplyCharacterSet(std::size_t index, GameLogic::Equipment equipment,
                                                                  const WorldEntryResources& resources);
} // namespace Client::Scenes
