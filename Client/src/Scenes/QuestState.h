#pragma once

#include "Data/Tables/QuestTable.h"

#include <array>
#include <cstddef>
#include <cstdint>

// El objeto de las misiones del original, [0x710148] (un global estático que arma 0x41cb60 desde 0x418ce0; vtable
// 0x6b89a0). Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Las misiones". El estado de la ventana,
// +0x21383, vive en UI::DelegatedPanels::quest, porque el registro de los paneles lo consulta como el panel 8.
namespace Client::Scenes
{
// +0x2134c: los bytes de estado que copia el A0 (0x418e20 pone en 0 doce dwords y un word).
inline constexpr std::size_t QuestStateBytes = 50;
// +0x21383: 1 es la ventana del diálogo con el NPC (0x4198b0; CanOpen no abre nada con ella), 2 la lista (0x419840).
inline constexpr std::uint8_t QuestDialogWindow = 1;
inline constexpr std::uint8_t QuestListWindow = 2;
// +0x21386: los estados de una misión (dos bits cada una) y el 5 de 0x418fb0 cuando una condición no se cumple.
inline constexpr std::uint8_t QuestNotStarted = 0;
inline constexpr std::uint8_t QuestInProgress = 1;
inline constexpr std::uint8_t QuestDone = 2;
inline constexpr std::uint8_t QuestAvailable = 3;
inline constexpr std::uint8_t QuestConditionFailed = 5;
// +4 antes del primer A0: el primer paso del juego (0x63693a) y la llegada a un NPC (0x5676a6) mandan el A0 con él.
inline constexpr std::uint8_t QuestClassUnknown = 0xff;

struct QuestState
{
    // +4 y +5: la clase & 7 y la clase >> 3 del héroe, que copian el A0 (0x418e20) y 0x419600. 0x41cb60 pone +4 en
    // 0xff y 0x4c2480 (la vuelta a la selección) también.
    std::uint8_t characterClass = QuestClassUnknown;
    std::uint8_t evolution = 0;
    // +6 a +9: los que escribe el 9F (0x4ddaa0) con pkt[3] de 0 a 3. Nadie más los lee en lo portado (*estático*).
    std::array<std::uint8_t, 4> flags{};
    // +0xc: los 200 registros de quest.bmd (0x418d10).
    Data::Tables::QuestTable records = Data::Tables::QuestTable(Data::Tables::QuestCount);
    // +0x2134c: dos bits por misión, como los manda el GS, aunque 0x419560 los lee corridos (Q-C841).
    std::array<std::uint8_t, QuestStateBytes> states{};
    // +0x2137e: la misión en curso (un byte: 0 − 1 es 255, Q-C842).
    std::uint8_t current = 0;
    // +0x2137f: la más alta que llegó del servidor.
    std::uint8_t reached = 0;
    // +0x21380 y +0x21381: los ponen en 0 0x419840 y 0x4c2480; 0x41b9e0 los lee para el ítem de la lista.
    std::uint8_t byte21380 = 0;
    std::uint8_t byte21381 = 0;
    // +0x21382: lo ponen en 0 0x41cb60 y 0x4c2480; nadie lo lee en lo portado.
    std::uint8_t byte21382 = 0;
    // +0x21384: el diálogo de Dialog.bmd que muestra la ventana (0x418f40, o el de la condición que falló).
    std::int16_t dialog = 0;
    // +0x21386: el estado de la misión en curso (0x419560(−1)), o 5.
    std::uint8_t currentState = 0;
    // +0x21387: relleno. Lo leen 0x419560 con las misiones 236 a 239 y lo pisan el A1 y el A2 de esas misiones o un A0
    // de más de 59 bytes (Q-C1126).
    std::uint8_t byte21387 = 0;
    // +0x21388: el zen que pide la condición (0x418fb0).
    std::uint32_t zen = 0;
    // +0x2138c: la pestaña de la lista (0x41b170 solo atiende la 0).
    std::uint8_t tab = 0;
    // +0x2138d: el A0 lo pone en 1 para que 0x419600 vuelva a copiar la clase del héroe.
    std::uint8_t refreshClass = 0;
    // +0x21390 y +0x21394: la esquina de la ventana (0x41cb60: 0x1c2 y 0; 0x41b5a0 los pone en cada paso).
    std::int32_t x = 0x1c2;
    std::int32_t y = 0;
};
} // namespace Client::Scenes
