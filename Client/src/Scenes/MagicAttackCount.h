#pragma once

#include "Core/Expected.h"
#include "Protocol/Viewport.h"

#include <cstdint>
#include <string>

// La skill que pega a varios (CL9 hito 2): el 1A, 0x4cedd0. Contrato: Client/docs/research/06-render-escenas-y-ui.md,
// "La skill que pega a varios".
namespace Client::Scenes
{
struct WorldEntryResources;

// 0x4cedd0: el que pega es el de la clave (0x5110d0, o el 400 fantasma: Q-C778) y queda en [0x5767964]; su brillo de
// la mano (0x53d810, RaiseTheHandShine), el sonido 0x76 (0x41ed10, en lastSoundId), la acción 0x96 (0x4ea030, con las
// acciones de su modelo), la skill en su +0x38e (abierto por la tabla: un rand() al cerrarse) y +0x381 en 1. Después,
// por cada blanco (0x5110d0, también el fantasma): rand() par, el golpe de 0x4f53a0 con su +0x384; con +0x384, el
// número de daño (0x548e40, amarillo naranja de 15, que sube) y, si la clave es la del jugador (`playerKey`,
// [0x57678bc]), la vida (+0x22) del registro propio baja +0x384 hasta 0 (abierto y cerrado por la tabla: 3.848
// rand() al cerrarse). Sin el modelo del que pega o con un golpe que corta, corta con la dirección.
[[nodiscard]] Core::Expected<void, std::string>
ApplyMagicAttackCount(const Protocol::Viewport::MagicAttackCount& attack, std::uint16_t playerKey,
                      const WorldEntryResources& resources);
} // namespace Client::Scenes
