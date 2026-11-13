#pragma once

#include "Core/Expected.h"
#include "Data/Tables/QuestTable.h"
#include "Data/Text/TextTable.h"
#include "Scenes/QuestState.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}

// Las funciones del objeto de las misiones [0x710148] (QuestState) que no dibujan: el estado que llega en el A0, el
// A1 y el A2, las condiciones de quest.bmd, los ítems pedidos y la apertura de la ventana. Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "Las misiones", y Q-C840 a Q-C845 en 07.
namespace Client::Scenes
{
class WorldState;
struct OwnInventory;

// Lo que leen las funciones que abren el registro propio o muestran un diálogo.
struct QuestContext
{
    WorldState& world;
    // El rand() de la CRT: los cierres del registro propio (3.848 cada uno que lo deja en 0).
    Render::Models::CrtRandom& random;
    // Text.bmd: el 609 de las respuestas vacías de 0x419710. nullptr: un diálogo sin respuestas corta.
    const Data::Text::TextTable* texts = nullptr;
};

// +0x2d + clase + 0x14 · pedido del registro (0x418f58, 0x418fe3, 0x4194ef, 0x419b68 y, en el dibujo de la ventana,
// 0x41b8b9): la marca de la clase & 7 de +4, que no se recorta. Una clase 5, 6 o 7 lee los diálogos del mismo pedido,
// y la 0xff de antes del primer A0 (0x41cb60 y 0x4c2480) lee 0xff bytes después de la primera marca: otro pedido o una
// condición (*estático*). Los dos bytes de relleno de +0x166, que MuRet no guarda, no se alcanzan con ninguna clase
// (0x14 · pedido + 7 + clase no da 0x140 ni 0x141).
[[nodiscard]] std::uint8_t QuestClassFlagOf(const Data::Tables::QuestRecord& quest, std::size_t entry,
                                            std::uint8_t characterClass);

// 0x4195c0(índice) (y 0x419560 con un índice): el byte índice / 4 (división con signo) corrido 2 · ((índice − índice /
// 4) & 0xf) bits, & 3. El SHR de 8 bits da 0 con una cuenta de 8 o más: solo las misiones 0 a 3 se leen donde las
// escribe el GS (Q-C841). Del 200 al 255 lee los campos que siguen a los 50 bytes (+0x2137e a +0x2138b): la 255 de la
// vuelta de Q-C842 lee el byte alto del zen de +0x21388, 0 con el quest.bmd real. +0x21383 y +0x21387, que QuestState
// no tiene, y un índice negativo o desde 256 dan 0 en MuRet.
[[nodiscard]] std::uint8_t QuestStateOf(const QuestState& quest, std::int32_t index);

// 0x419560(−1): el estado de la misión en curso (+0x2137e, sin signo) en +0x21386.
void ReadTheCurrentQuestState(QuestState& quest);

// 0x558410(tipo, cuenta, nivel): las 64 casillas del inventario con ese tipo (el short de +0, con signo), ocupadas
// (+0x3c > 0) y, si el nivel no es −1, con ((+4 >> 3) & 0xf) igual. 0 si llega a la cuenta; si no, cuántos faltan.
[[nodiscard]] std::int32_t MissingQuestItems(const OwnInventory& inventory, std::int32_t type, std::int32_t count,
                                             std::int32_t level);

// 0x4ddad0 → 0x418e20(pkt + 4, pkt[3], clase del héroe): la clase (& 7 y >> 3) en +4 y +5, +0x2138d en 1 y 0x6eeda8 en
// 0 (la clase llega como un byte sin signo: nunca es −1); los 50 bytes de estado en 0 y `count` bytes del paquete
// encima; la primera misión de las `count` que no está terminada en +0x2137e y +0x2137f; y +0x390 del héroe en 1 si la
// misión 3 está terminada. `bytes` son los que trae el paquete después de pkt[3]: el original copia `count` aunque el
// paquete sea más corto (el GS 0.97 manda la cuenta sin los bytes, Q-C840); lo que falta va en 0 (P-C171). Una cuenta
// de más de 50 pisaría +0x2137e y lo que sigue: corta.
[[nodiscard]] Core::Expected<void, std::string> ApplyTheQuestList(WorldState& world, std::uint8_t count,
                                                                  std::span<const std::uint8_t> bytes);

// 0x418ee0(índice, estado), del A1 y del A2: +0x2137e = índice, +0x2137f = el mayor, el byte índice >> 2 entero con
// `state` (el GS manda el byte con las cuatro misiones, Q-C843) y +0x390 del héroe según la misión 3. Un índice de 200
// o más escribiría fuera de los 50 bytes: corta.
[[nodiscard]] Core::Expected<void, std::string> SetTheQuestState(WorldState& world, std::uint8_t index,
                                                                 std::uint8_t state);

// 0x418f40(registro, lugar): el diálogo `slot` (0 a 3) del primer pedido con la marca de la clase de +4. Sin ninguno,
// −−+0x2137e, 0x419600(0xff) y +0x21384. Un registro fuera de los 200 corta (0 − 1 es 255, Q-C842).
[[nodiscard]] Core::Expected<std::int16_t, std::string> QuestDialogOf(const QuestContext& context, std::size_t record,
                                                                      std::size_t slot);

// 0x4194c0(registro): falso (y el diálogo de faltan ítems, 0x418f40(registro, 1), en +0x21384) si a algún pedido de
// ítems de la clase le faltan.
[[nodiscard]] Core::Expected<bool, std::string> HasTheQuestItems(const QuestContext& context, std::size_t record);

// 0x418fb0(registro, conZen): para cada pedido de la clase, cada condición enlazada (+1 igual al +6 del pedido, o
// 0xff): la misión pedida terminada; el nivel mínimo y el máximo con el registro propio abierto y cerrado una vez cada
// uno; y el zen: sin `checkZen` solo lo guarda en +0x21388; con él, lo lee con el registro abierto, lo guarda y lo
// compara. La primera que no se cumple deja su diálogo en +0x21384 y 5 en +0x21386, y da falso.
[[nodiscard]] Core::Expected<bool, std::string> CheckTheQuestConditions(const QuestContext& context, std::size_t record,
                                                                        bool checkZen);

// 0x419600(estado): la clase de nuevo si +0x2138d (el chequeo de la clase contra −1 nunca da, Q-C844); con 0xff el
// estado de la misión en curso (0x419560(−1)), si no `state` en +0x21386; y el diálogo de ese estado. Devuelve
// +0x21386.
[[nodiscard]] Core::Expected<std::uint8_t, std::string> RefreshTheQuest(const QuestContext& context,
                                                                        std::uint8_t state);

// 0x419b10: con +0x21386 en 1, verdadero si hay algún pedido de ítems de la clase y a ninguno le faltan. Corta con la
// misión en curso fuera de los 200 registros (Q-C842) o más de 16 pedidos.
[[nodiscard]] Core::Expected<bool, std::string> CanCompleteTheQuest(const WorldState& world);

// 0x4198b0(misión): la misión (−1: la que está), 0x78abb00 en 0, la ventana en 1 (el diálogo con el NPC),
// 0x419600(0xff) y 0x419710 con +0x21384.
[[nodiscard]] Core::Expected<void, std::string> OpenTheQuestDialog(const QuestContext& context, std::int32_t quest);

// 0x419840(misión), de 0x6599a0(8): +0x21380 y +0x21381 en 0, la ventana en 2 (la lista), y 0x419600(0xff) y 0x419710
// con la más alta (+0x2137f) en +0x2137e, que después vuelve a la de antes.
[[nodiscard]] Core::Expected<void, std::string> OpenTheQuestList(const QuestContext& context, std::int32_t quest);

// 0x4c2480 (0x4c268a..0x4c26d2), la vuelta a la selección: +4 en 0xff (el A0 se vuelve a pedir, Q-C215), +0x21381,
// +0x21384, la ventana, +0x21382, +0x2137e y +0x2137f en 0, los 50 bytes de estado y +6..+8 en 0 (+9 queda).
void ResetTheQuests(QuestState& quest, std::uint8_t& window);
} // namespace Client::Scenes
