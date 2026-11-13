#pragma once

#include "Core/Expected.h"
#include "Data/Text/DialogTable.h"
#include "Data/Text/TextTable.h"
#include "UI/Dialogs.h"
#include "UI/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Las cajas viejas del juego (06 § HUD y ventanas, "Los diálogos del juego", CL8 hito 6): el texto y los dos botones
// que arman 0x632e30 y 0x633300, los que pasa 0x623e70 (0x62d756) y dibuja 0x62db60. Todos globales de la .bss.
namespace Client::UI
{
// 0x632e30 y 0x633300 parten en a lo sumo 7 renglones (0x632c90(texto, 0x7d36a98, 7, 0x26)).
inline constexpr std::size_t DialogRowCount = 7;
// 0x26 bytes por renglón, con su '\0'.
inline constexpr std::size_t DialogRowBytes = 0x26;
// 0x7d368b8 y 0x7d368cc: dos botones de 5 dwords (0x62d7e1 corta en 0x7d368e4).
inline constexpr std::size_t DialogButtonCount = 2;
// 0x632e30: el aviso de un botón.
inline constexpr std::int32_t MessageBoxDialog = PanelFriendlyDialog;
// 0x62d7eb: el diálogo con páginas.
inline constexpr std::int32_t PagedDialog = 0x8c;
// 0x633300: los textos que se arman con su propio formato (0x63330f, 0x633327 y 0x63340d) o con 0x632c90 seguidos
// (0x6333a8).
inline constexpr std::int32_t MoneyTextDialog = 0x99;
inline constexpr std::int32_t ServerTimeDialog = 0xa3;
inline constexpr std::int32_t ItemNameDialog = 0x96;
inline constexpr std::int32_t ItemNameWithLuckDialog = 0xa8;
// 0x632f70: el diálogo de Dialog.bmd con respuestas (el de los NPC, CL9 hito 2).
inline constexpr std::int32_t AnswersDialog = 0x8d;
// 0x7d3691c: las respuestas de 0x419710 y 0x632f70, 10 renglones de 0x26 (0x5f dwords en 0).
inline constexpr std::size_t DialogAnswerRows = 10;
// 0x419710 y 0x632f70: sin respuestas, "1) " y el texto 609 de Text.bmd (0x76e9b88).
inline constexpr std::size_t NoAnswersText = 609;

// Un botón de 0x7d368b8: el estado (0 no está; 1 afuera; 2 con el mouse encima; 0x633300 arranca el segundo en 3),
// y el rectángulo, que 0x62d756 corre (0xd5, 0x3c).
struct DialogButton
{
    std::int32_t state = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

struct DialogBoxState
{
    // 0x7d36a98: 7 renglones de 0x26 bytes seguidos. Un strcpy largo de 0x633300 sigue en el renglón siguiente.
    std::array<char, DialogRowCount * DialogRowBytes> rows{};
    // 0x7d368e4: los renglones que dibuja 0x62db60.
    std::int32_t lineCount = 0;
    std::array<DialogButton, DialogButtonCount> buttons{};
    // 0x7d3a0f4, 0x7d3a0f8 y 0x7d3a0f9: el primer texto del diálogo con páginas, la página (un char con signo) y la
    // última. Los escribe quien lo abre (CL9).
    std::int32_t pageText = 0;
    std::int8_t page = 0;
    std::int8_t lastPage = 0;
    // 0x7d3691c: las respuestas, "%d) %s" partido en un renglón cada una. El de sin respuestas va con strcpy y puede
    // seguir en el siguiente.
    std::array<char, DialogAnswerRows * DialogRowBytes> answers{};
    // 0x7d3a0fc: cuántas (la lista del 0x8d y de la ventana de las misiones).
    std::int32_t answerCount = 0;
    // 0x6d5e28 (.data, −1): el registro de Dialog.bmd que muestran.
    std::int32_t dialogIndex = -1;
};

// La respuesta `row` como la lee el dibujo: desde su primer byte hasta el primer '\0', que puede estar en la
// siguiente. MuRet corta al final de las 10.
[[nodiscard]] std::string_view DialogAnswer(const DialogBoxState& box, std::size_t row);

// El renglón `row` como lo lee el dibujo: desde su primer byte hasta el primer '\0', que puede estar en el renglón
// siguiente. Sin '\0' hasta el final de los 7, el original seguiría leyendo la .bss: MuRet corta ahí.
[[nodiscard]] std::string_view DialogRow(const DialogBoxState& box, std::size_t row);

// 0x632e30(texto): `text` partido con 0x632c90 en los renglones, los botones en 0 y el primero en (1, 0x47, 0x8c,
// 0x46, 0x15), y el 0x8a abierto, o esperando si hay otro (sin los casos de 0x6236f0).
void OpenMessageBox(DialogBoxState& box, DialogState& dialogs, std::string_view text);

// 0x633300(renglones, primerTexto, id): los textos `firstText` .. `firstText + lineCount − 1` de Text.bmd en los
// renglones, con un strcpy cada uno (0x9a los parte con 0x632c90 uno detrás del otro), los botones (1, 0x15, 0x5a,
// 0x46, 0x15) y (3, 0x78, 0x5a, 0x46, 0x15), con y = 0x78 en el 0x9a, y el id por 0x6236f0. Los de 0x99, 0xa3, 0x96
// y 0xa8 arman su texto con un formato y cortan con su dirección (CL9.3). Lo que se escribiría fuera de los 7
// renglones corta, como un texto que no está.
[[nodiscard]] Core::Expected<void, std::string> OpenTextDialog(DialogBoxState& box, DialogState& dialogs,
                                                               const Data::Text::TextTable* texts,
                                                               std::int32_t lineCount, std::int32_t firstText,
                                                               std::int32_t id);

// 0x419710(índice), lo que muestra la ventana de las misiones: [0x6d5e28] = índice, el texto de Dialog.bmd partido en
// los 7 renglones (0x632c90), las respuestas en 0 y, por cada una, "%d) %s" (wsprintf con 0x6c64b8) en un renglón; sin
// ninguna (la cuenta en 0), "1) " y el texto 609 con strcpy y la cuenta en 1. Después, 0x6236f0(0). No toca los
// botones. Corta sin la tabla, con un índice fuera de los 200 o sin el texto 609.
[[nodiscard]] Core::Expected<void, std::string> ShowTheQuestDialog(DialogBoxState& box, DialogState& dialogs,
                                                                   const Data::Text::DialogTable* table,
                                                                   const Data::Text::TextTable* texts,
                                                                   std::int32_t index);

// 0x632f70(índice): lo mismo que 0x419710 con los botones en 0 (0x7d368b8, 10 dwords) y, en vez de 0x6236f0(0), el
// 0x8d abierto, o esperando si hay otro.
[[nodiscard]] Core::Expected<void, std::string> OpenTheAnswersDialog(DialogBoxState& box, DialogState& dialogs,
                                                                     const Data::Text::DialogTable* table,
                                                                     const Data::Text::TextTable* texts,
                                                                     std::int32_t index);

// 0x62d756..0x62d7e9: los dos botones. Con estado 1 o más, el mouse en [x + 0xd5, x + w + 0xd5] × [y + 0x3c, y + h +
// 0x3c] (con los bordes) lo pone en 2 y, con el clic (0x7d366d4), lo apaga, apaga el botón abajo (0x7d36874) y lo
// elige; afuera, en 1. Devuelve el último elegido, o −1.
[[nodiscard]] std::int32_t StepTheDialogButtons(DialogBoxState& box, Point mouse, bool& click, bool& leftHeld);

// 0x62d7eb..0x62d8fb: las flechas del 0x8c, con el clic y sin apagarlo. La de la izquierda ([0xf9, 0x108] × [0xca,
// 0xde]) resta una página sin bajar de 0; la de la derecha ([0x179, 0x188] × [0xca, 0xde]) suma una sin pasar de la
// última. Con cualquiera, los renglones son los del texto pageText + page (0x632c90). Antes, 0x620a00(1), que en el
// paso no dibuja nada (*inferido*, 08).
[[nodiscard]] Core::Expected<void, std::string>
StepTheDialogPages(DialogBoxState& box, const Data::Text::TextTable* texts, Point mouse, bool click);
} // namespace Client::UI
