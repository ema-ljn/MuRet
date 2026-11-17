#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Protocol::Quest
{
struct QuestFlag;
struct QuestList;
struct QuestStateChange;
struct SetQuestResult;
struct QuestResult;
} // namespace Client::Protocol::Quest

// Los 9F y A0..A3 que llegan (CL9 hito 2), sobre el objeto de las misiones de WorldRuntime. Contrato: 04 § Las
// misiones y los NPC, y 06, "Las misiones".
namespace Client::Scenes
{
class WorldState;
struct WorldEntryResources;

// 0x4ddaa0: con pkt[3] de 0 a 3, +6 + pkt[3] de [0x710148] = pkt[4]; con otro, nada.
void ApplyQuestFlag(const Protocol::Quest::QuestFlag& flag, WorldState& world);

// 0x4ddad0 → 0x418e20 (ApplyTheQuestList). Lo que la copia escribiría después del objeto se descarta con un aviso en
// resources.log (Q-C1126).
void ApplyQuestList(const Protocol::Quest::QuestList& list, const WorldEntryResources& resources);

// 0x4ddb00: 0x418ee0(pkt[3], pkt[4]), 0x657a90 (todos los paneles: las misiones abiertas mandan el C1 03 31 con
// resources.sendWindowClose) y 0x4198b0(−1), la ventana del diálogo con el NPC. Un registro fuera de los 200 en el
// diálogo (Q-C842) descarta lo que falta con un aviso en resources.log, sin cortar.
[[nodiscard]] Core::Expected<void, std::string> ApplyQuestState(const Protocol::Quest::QuestStateChange& change,
                                                                const WorldEntryResources& resources);

// 0x4ddb40: con pkt[4] en 0, lo mismo que el A1 con pkt[3] y pkt[5]; con otro, nada.
[[nodiscard]] Core::Expected<void, std::string> ApplySetQuestResult(const Protocol::Quest::SetQuestResult& result,
                                                                    const WorldEntryResources& resources);

// 0x4ddb80: el sonido 0x66 (lastSoundId, CL10) y el personaje de la clave (0x5110d0: el 400 sin ninguno). Con 0xc8 y
// 0xca, el héroe suma pkt[6] a los puntos de +0x68 del registro propio; con 0xc9, la clase ((pkt[6] & 0x10) | (pkt[6]
// >> 4)) >> 1 en +0x1d4 del personaje y, si es el héroe, en +0xb del registro propio. Cada acceso al registro propio
// lo abre y lo cierra (3.848 rand() si lo deja en 0). Las cuatro siguen con los joints 0x60f (0x530860), el efecto
// 0x61e (0x518cf0), y la 0xc9, la 0xca y la 0xcb con 0x513100 y la acción 0xb8: cortan en el primer joint (CL11).
// Con otro tipo, nada más.
[[nodiscard]] Core::Expected<void, std::string> ApplyQuestResult(const Protocol::Quest::QuestResult& result,
                                                                 const WorldEntryResources& resources);
} // namespace Client::Scenes
