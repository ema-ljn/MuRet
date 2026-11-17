#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Protocol::Viewport
{
struct Monsters;
struct Summons;
struct Objects;
} // namespace Client::Protocol::Viewport

// Los monstruos y los NPC que entran al viewport (CL9 hito 2): el 0x13, el 0x1F y el 0x68 sobre CreateMonster
// (0x5141e0). Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Los monstruos y los NPC en el viewport".
// *Estático*. Lo que MuRet no tiene corta con error, con su dirección, después de lo que el original hace antes.
namespace Client::Scenes
{
struct ActorState;
struct WorldEntryResources;

// 0x4c9790, el recién llegado por +0x376: el 0x15 y el 0x2c con la acción 1 (0x4ea030, que necesita el esqueleto del
// modelo), +0x112 en 1, el alpha en 1 y el sonido 0x13 o 0x14e; el 0x35 y el 0x36 cortan en 0x4c982b (+0x3e5 en 0x3c,
// el freno con efectos y rand() de 0x505de0, sin portar); el 0x55, 0x5b, 0x61, 0x72, 0x78 y 0x7e piden el sonido 0x173;
// el 0x154, 0x155, 0x158 y 0x159 cortan en 0x4c9883 (leen [0x6e5904], P-C60 g, y sueltan joints de la CL11). Los demás:
// con el modelo 0x20a, la acción 0xc1, +0x112 en 0xc1 y el alpha en 1; si no, el alpha en 0. Los sonidos van a
// WorldRuntime::lastSoundId (CL10).
[[nodiscard]] Core::Expected<void, std::string> ArriveAsNew(ActorState& actor, const WorldEntryResources& resources);

// 0x4c9c60, cada registro: CreateMonster(tipo, X, Y, clave) (un error corta el lazo), los estados (0x4e6380), el
// ángulo (dirección − 1) · 45 (con el modelo 0x1b5, por 0 con la Y de su celda en 0x5a, por −135 con la X en 0x3e y por
// 90 con la X en 0xb7, leídas sin abrir: lo que dejó el cierre, ActorState::cellXLeft: con los cierres en 0 no da
// nunca, Q-C849), la celda protegida, +0x39a/+0x39b, +0x42d con el bit 7 del tipo alto y +0x93 con sus bits 4..6. En
// Castle Siege (30) el +0x376 0x115 corta en 0x4c9f0f (0x4ea030 y 0x42a110, CL9.5) si +0x78 tiene 0x20000 o es 0.
// Recién llegado, ArriveAsNew; si no, el camino (WalkToTheTarget).
[[nodiscard]] Core::Expected<void, std::string> ApplyViewportMonsters(const Protocol::Viewport::Monsters& monsters,
                                                                      const WorldEntryResources& resources);

// 0x4ca0b0, cada registro: los tipos 0x98..0x9e por 0x517400 (CreateMonster, el nivel en tipo − 0x97, el nombre por
// wsprintfA con el del dueño, +4 en 1 con el nivel 7; recién llegado corta en 0x51747c, los rand() y los joints 0x615
// de la CL11) y los demás por CreateMonster; los estados, el ángulo, la celda protegida, +0x39a/+0x39b, +0x88 en 1, el
// PK (dirección & 0xf) y, con 6 o más, el nivel en 1. Fuera de 0x98..0x9e, el nombre es el del dueño (diez bytes, con
// +0x1e3 en 0), el Text 485 y el del monstruo (strcat). Recién llegado: ArriveAsNew y corta en 0x4ca5f6 (el efecto 0xec
// de 0x518cf0 y el joint 0x5d5 de 0x53fe70, CL11); si no, el camino.
[[nodiscard]] Core::Expected<void, std::string> ApplySummons(const Protocol::Viewport::Summons& summons,
                                                             const WorldEntryResources& resources);

// 0x4e0980, cada registro por su +0: con 1, el alta de un jugador (CreateActor, 0x20a, ángulo 0) con la clase de +1, la
// piel y el PK en 0, +0x88 en 0x80, los estados, la celda protegida, +0x39a/+0x39b, +0x428 en 0x96, el ángulo rand() %
// 360, el equipo (ApplyCharacterSet) y el nombre "   " con +0x1e3 en 0; con 2 o 3, CreateMonster con el tipo de +1 y
// +2, los estados, la celda, +0x39a/+0x39b, +0x428 y el ángulo, y el 0x115 de Castle Siege como el 13; con otro, nada.
[[nodiscard]] Core::Expected<void, std::string> ApplyViewportObjects(const Protocol::Viewport::Objects& objects,
                                                                     const WorldEntryResources& resources);
} // namespace Client::Scenes
